/*
 * mpserver.c
 *
 * all that is needed to share player information via HTTP
 *
 *  Created on: 01.05.2018
 *      Author: B.Weber
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netdb.h>
#include <poll.h>

#include "mpserver.h"
#include "mpcomm.h"
#include "controller.h"
#include "config.h"
#include "utils.h"
#include "database.h"
#include "json.h"

/* build/ paths are relative to Makefile and needed to create proper
   dependencies even if the are misleading in src/ */
#include "build/mpplayer_html.h"
#include "build/mpplayer_js.h"
#include "build/mixplay_html.h"
#include "build/mprc_html.h"
#include "build/mixplay_js.h"
#include "build/mixplay_css.h"
#include "build/mixplay_svg.h"
#include "build/mixplay_png.h"
#include "build/manifest_json.h"
#include "build/bookmarklet_js.h"

#ifndef VERSION
#define VERSION "developer"
#endif

/* message offset */
#define MPV 10

/* lock for fname, flen, fdata and mtype */
static pthread_mutex_t _sendlock = PTHREAD_MUTEX_INITIALIZER;

static uint32_t _numclients = 0;
static uint32_t _heartbeat[MAXCLIENT];	/* glabal clientID marker */

/* for search polling and retrying */
static const struct timespec ts = {
	.tv_nsec = 250000,
	.tv_sec = 0
};

#define CL_STP 0				// client is dying, skip all following steps
#define CL_ONE 1				// one-shot, only do one receive-send flow
#define CL_RUN 2				// client is running, loop receive-send flow

#define ROUNDUP(a,b) (((a/b)+1)*b)

/* all the static files that we are prepared to serve */
typedef enum {
	f_none = -1,
	f_index = 0,
	f_mprc,
	f_mpcss,
	f_mpjs,
	f_mpico,
	f_mppl,
	f_mppng,
	f_mppljs,
	f_mani
} filedefs;

typedef struct {
	const char *fname;
	const uint8_t *fdata;
	const size_t flen;
	const char *mtype;
} fileinfo_t;

/* the kind of request that came in */
typedef enum {
	req_none = 0,
	req_update,
	req_command,
	req_file,
	req_config,
	req_version,
	req_mp3,
	req_current,
	req_stop
} httpstate;

/* roughly: "ARTIST - TITLE.mp3\0" */
#define FNLEN (NAMELEN+3+NAMELEN+4+1)

/* the parameter set that needs to be accesses by all parts */
typedef struct {
	int sock;
	char *commdata;
	ssize_t commsize;			// the size of the commdata array
	ssize_t commlen;			// length of data coming from the browser
	uint32_t running;
	mptitle_t *title;
	httpstate state;
	int32_t fullstat;
	filedefs filedef;
	int32_t cmd;
	char *arg;
	int32_t clientid;
	char fname[FNLEN];
	size_t len;					// if set, size of data in commdata to send back to the browser
	char boundary[NAMELEN];
	size_t filesz;
	size_t filerd;
	int filefd;
	char fpath[MAXPATHLEN];
} chandle_t;


/**
 * wrapper around send() taking care of partial sends, retrying to send on
 * partials. Returning
 * 0 on error
 * sent bytes on success
 * -sent bytes on partial success
 */
static ssize_t sendloop(int fd, const void *data, ssize_t len) {
	ssize_t pos = 0;
	ssize_t sent = 0;

	while (pos < len) {
		sent = send(fd, (const char *) data + pos, len - pos, 0);
		if (sent > 0) {
			pos += sent;
		}
		else {
			addMessage(MPV + 1, "send failed");
			sent = -pos;
			break;
		}
	}
	return sent;
}

/* return an unused clientid
   this gives out clientids in ascending order, even if a previous clientid
   is already free again. This is done to avoid mobile clients reconnecting
   with their old clientid causing mix-ups if that id was already recycled.
   It still may happen but there need to be ~100 connects while the client
   was offline. Good enough for now */
static int32_t getFreeClient(void) {
	static uint32_t maxclientid = 0;
	int32_t i;

	for (i = 0; i < MAXCLIENT; i++) {
		int32_t clientid = (maxclientid + i) % MAXCLIENT;

		if (_heartbeat[clientid] == 0) {
			_heartbeat[clientid] = 1;
			_numclients++;
			addMessage(MPV + 2, "client %i connected, %i clients connected",
					   clientid + 1, _numclients);
			maxclientid = clientid + 1;
			return clientid + 1;
		}
	}
	addMessage(0, "Out of clients!");
	return -1;
}

/*
 * whenever a client sends a message, it will reset it's idle counter and
 * all other clients will be decreased. If the idle counter hits zero
 * the connection is considered dead.
 */
static void triggerClient(int32_t client) {
	int32_t run;

	client--;

	for (run = 0; run < MAXCLIENT; run++) {
		if (run == client) {
			if (_heartbeat[run] == 0) {
				/* client remembered it's ID while server considered it dead */
				addMessage(MPV + 0, "Client %" PRId32 " got resurrected!",
						   run + 1);
				_numclients++;
			}
			/* all other clients have done at least two updates + 10 to drown out
			 * volume requests */
			_heartbeat[run] = (2 * _numclients) + 10;
		}
		else {
			if (_heartbeat[run] > 0) {
				_heartbeat[run]--;
				if (_heartbeat[run] == 0) {
					_numclients--;
					/* there MUST be at least one active client as one just
					 * invoked this function! If we see this, we probably
					 * need to mutex lock the client functions... */
					if (_numclients < 1) {
						addMessage(-1, "Client count out of sync!");
						_numclients = 1;
					}
					/* make sure that the server won't get blocked on a dead client */
					if ((run + 1) == getCurClient()) {
						addMessage(0, "Client %i died while being locked. What kept it waiting for so long?!", getCurClient());
						assert(false);
					}
					addMessage(MPV + 2,
							   "client %i disconnected, %i clients connected",
							   run + 1, _numclients);
				}
			}
		}
	}
}

bool deadClient(uint32_t cid) {
	return (_heartbeat[cid - 1] == 0);
}

/**
 * send a static file
 */
static int32_t filePost(int32_t sock, const char *fname) {
	int32_t fd;

	fd = open(fname, O_RDONLY);
	if (fd != -1) {
		errno = 0;
		while (sendfile(sock, fd, NULL, 4096) == 4096);
		if (errno != 0) {
			addMessage(0, "Error %s sending %s!", strerror(errno), fname);
		}
		close(fd);
	}
	else {
		addMessage(0, "%s not found!", fname);
	}
	return 0;
}

/**
 * decodes parts in the form of %xx
 * In fact I only expect to see %20 in search strings but who knows..
 * and it WILL certainly break soon too..
 * also considers \n or \r to be line end characters
 */
static char *strdec(char *target, const char *src) {
	uint32_t i, j;
	char buf = 0;
	int32_t state = 0;

	for (i = 0, j = 0; i < strlen(src); i++) {
		if ((src[i] == 0x0d) || (src[i] == 0x0a)) {
			break;
		}
		switch (state) {
		case 0:
			if (src[i] == '%') {
				state = 1;
			}
			else {
				target[j++] = src[i];
			}
			break;
		case 1:
			buf = 16 * hexval(src[i]);
			state = 2;
			break;
		case 2:
			buf = buf + hexval(src[i]);
			target[j++] = buf;
			state = 0;
			break;
		}
	}

	/* cut off trailing blanks */
	target[j--] = 0;
	while (j > 0 && isblank(target[j])) {
		target[j--] = 0;
	}
	return target;
}

static int32_t fillReqInfo(chandle_t * info, char *line) {
	jsonObject *jo = NULL;
	char *jsonLine = (char *) falloc(strlen(line), 1);
	int32_t rc = 0;

	strdec(jsonLine, line);
	addMessage(MPV + 3, "received request: %s", jsonLine);
	jo = jsonRead(jsonLine);
	free(jsonLine);
	if (jsonPeek(jo, "cmd") == json_error) {
		rc = 1;
	}
	else {
		info->cmd = jsonGetInt(jo, "cmd");
		sfree(&(info->arg));
		if (jsonPeek(jo, "arg") == json_string) {
			info->arg = jsonGetStr(jo, "arg");
			/* strip especially trailing spaces from mobile clients */
			instrip(info->arg);
			if (strlen(info->arg) == 0) {
				sfree(&(info->arg));
			}
		}
		info->clientid = jsonGetInt(jo, "clientid");
		addMessage(MPV + 3, "cmd: %i", info->cmd);
		addMessage(MPV + 3, "arg: %s", info->arg ? info->arg : "(NULL)");
		addMessage(MPV + 3, "cid: %i", info->clientid);
	}
	jsonDiscard(jo);
	return rc;
}

typedef enum {
	rep_continue = 0,
	rep_ok,
	rep_created,
	rep_bad_request,
	rep_not_found,
	rep_not_implemented,
	rep_unavailable,
} reply_t;

static void prepareReply(chandle_t * handle, reply_t reply, bool stop) {
	static const char *replies[] = {
		"HTTP/1.0 100 Continue\015\012Content-Length: 0\015\012\015\012",
		"HTTP/1.0 200 OK\015\012Content-Length: 2\015\012\015\012OK",
		"HTTP/1.0 201 Created\015\012Content-Length: 2\015\012Location: /\015\012\015\012OK",
		"HTTP/1.0 400 Bad Request\015\012Content-Length: 8\015\012\015\012Go Away!",
		"HTTP/1.0 404 Not Found\015\012Content-Length: 0\015\012\015\012",
		"HTTP/1.0 501 Not Implemented\015\012Content-Length: 8\015\012\015\012Go Away!",
		"HTTP/1.0 503 Service Unavailable\015\012Content-Length: 8\015\012\015\012Go Away!",
	};

	strcpy(handle->commdata, replies[reply]);
	handle->len = strlen(handle->commdata);
	handle->state = stop ? req_stop : req_none;
	if (reply == rep_bad_request) {
		notifyChange(MPCOMM_ABORT);
	}
}

/**
 * just poll on a socket and wait for data
 * read all the data into the handlers commdata field on success
 * do error handling otherwise.
 * 
 * returns true if data was delivered and false on none.
 */
static bool fetchRequest(chandle_t * handle) {
	struct pollfd pfd;

	pfd.fd = handle->sock;
	pfd.events = POLLIN;
	/* reset buffer */
	memset(handle->commdata, 0, handle->commsize);
	handle->commlen = 0;
	handle->len = 0;
	int deathcount = 0;

	/* Either an error or a timeout */
	while ((handle->running != CL_STP) && (poll(&pfd, 1, 250) <= 0)) {
		switch (errno) {
		case EINTR:
			/* the poll was interrupted by a signal,
			 * not worth bailing out */
			addMessage(MPV + 1, "poll(%i): Interrupt", handle->sock);
			break;
		case EBADF:
			addMessage(MPV + 1, "poll(%i): Dead Socket", handle->sock);
			handle->running = CL_STP;
			break;
		case EINVAL:
			addMessage(MPV + 1, "Invalid fds on %i", handle->sock);
			handle->running = CL_STP;
			break;
		case ENOMEM:
			addMessage(MPV + 1, "poll(%i): No memory", handle->sock);
			handle->running = CL_STP;
			break;
		default:
			if (deathcount++ > 7) {
				/* timeout, no one was calling for two seconds 
				 * commonly happens when a mobile client closes */
				addMessage(MPV + 0, "Reaping unused clienthandler for %i",
						   handle->clientid);
				handle->running = CL_STP;
				return false;
			}
		}
	}

	/* fetch data if any */
	if (pfd.revents & POLLIN) {
		ssize_t retval;

		do {
			retval =
				recv(handle->sock, handle->commdata + handle->commlen,
					 handle->commsize - handle->commlen, MSG_DONTWAIT);
			if (retval == -1) {
				/* check for deadly errors, EAGAIN and EWOULDBLOCK should just loop */
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
					nanosleep(&ts, NULL);
					retval = 0;
				}
				else {
					addMessage(MPV + 1, "Read error on socket!\n%s",
							   strerror(errno));
					/* The socket got broken so the client should terminate too */
					handle->running = CL_STP;
					return false;
				}
			}
			else if (retval == handle->commsize - handle->commlen) {
				/* read buffer is full, expand and go on */
				handle->commlen = handle->commsize;
				handle->commsize += MP_BLKSIZE;
				handle->commdata =
					(char *) frealloc(handle->commdata, handle->commsize);
				memset(handle->commdata + handle->commlen, 0, MP_BLKSIZE);
			}
			else {
				/* everything fit into the buffer, we're done */
				handle->commlen += retval;
				retval = -1;
			}
		} while (retval != -1);
	}

	/* zero bytes read this should probably also have
	 * death count so we don't do endless loopings */
	if (handle->commlen == 0) {
		if (handle->filefd > 0) {
			/* We expect data but nothing came in, try again */
			addMessage(MPV + 0, "Retrying for %i", handle->clientid);
			nanosleep(&ts, NULL);
			/* explicitly tell the client to continue */
			prepareReply(handle, rep_continue, false);
		}
		else {
			/* This is not really correct but unless I find out which reply
			 * chrome expects after successfully uploading form data we just
			 * kill off the connection.. */
			addMessage(MPV + 0, "Client %i disconnected prematurely",
					   handle->clientid);
			handle->running = CL_STP;
		}
		return false;
	}

	return true;
}

typedef enum {
	met_unset = 0,
	met_get,
	met_post,
	met_file,
	met_upload,
	met_datainit,
	met_dataflow
} method_t;

/**
 *  we got a connection, data came in and was stored, so now interpret that data
 *  to decide what the client wants. So check the headers and any payload 
 */
static void parseRequest(chandle_t * handle) {
	method_t method = met_unset;
	mpcmd_t cmd = mpc_idle;
	mpconfig_t *config = getConfig();

	addMessage(MPV + 3, "%s", handle->commdata);
	char *end = strchr(handle->commdata, ' ');
	char *pos = end + 1;

	if (strlen(handle->fname) > 0) {
		/* raw data for an active upload */
		pos = handle->commdata;
		method = met_dataflow;
	}
	else if ((handle->boundary[0] != '\0')
			 && (strcasestr(handle->commdata, handle->boundary) != NULL)) {
		/* start of raw data */
		pos = handle->commdata;
		method = met_datainit;
	}
	else if (end == NULL) {
		addMessage(MPV + 0, "Malformed HTTP: %s", handle->commdata);
		prepareReply(handle, rep_bad_request, true);
		return;
	}
	else {
		*end = 0;
		if (strcasecmp(handle->commdata, "get") == 0) {
			method = met_get;
		}
		else if (strcasecmp(handle->commdata, "post") == 0) {
			method = met_post;
		}
		else {
			addMessage(MPV + 0, "Unsupported method: %s", handle->commdata);
			prepareReply(handle, rep_not_implemented, true);
			return;
		}
	}

	/* check request for GET and POST */
	if ((method == met_get) || (method == met_post)) {
		/* parse the rest of the request */
		end = strchr(pos, ' ');
		if (end != NULL) {
			*(end + 1) = 0;
			/* has an argument? */
			char *arg = strchr(pos, '?');

			if (arg != NULL) {
				*arg = 0;
				arg++;
				if (fillReqInfo(handle, arg)) {
					addMessage(0, "Malformed arguments: %s", arg);
					prepareReply(handle, rep_bad_request, true);
					return;
				}
			}
		}

		/* new update client request */
		if (handle->clientid == -1) {
			handle->clientid = getFreeClient();
			if (handle->clientid == -1) {
				/* no free clientid - no service */
				addMessage(MPV + 0, "No free clientID");
				prepareReply(handle, rep_unavailable, true);
				return;
			}
			initMsgCnt(handle->clientid);
			addNotify(handle->clientid, MPCOMM_TITLES);
		}

		/* a valid client came in */
		if (handle->clientid > 0) {
			handle->running = CL_RUN;
			triggerClient(handle->clientid);
		}

		if (end == NULL) {
			addMessage(MPV + 0, "Malformed request %s", pos);
			prepareReply(handle, rep_bad_request, true);
			return;
		}
		/* control command */
		else if (strstr(pos, "/mpctrl/")) {
			pos = pos + strlen("/mpctrl");
		}
		else if (strstr(pos, "/upload")) {
			method = met_upload;
			pos = pos + strlen("/upload");
		}
		/* everything else is treated like a GET <path> */
		else {
			if (method == met_get) {
				method = met_file;
			}
			else {
				/* looking bad */
				addMessage(MPV + 0, "Illegal request %s", handle->commdata);
				prepareReply(handle, rep_bad_request, true);
				return;
			}
		}
	}

	/* so far we just checked the request line, that's enough for most cases */

	switch (method) {
	case met_get:				/* GET mpcmd */
		if (strcmp(pos, "/status") == 0) {
			handle->state = req_update;
			addMessage(MPV + 2, "Statusrequest: 0x%x", handle->fullstat);
		}
		else if (strstr(pos, "/title/") == pos) {
			pos += 7;
			int index = atoi(pos);

			if ((config->current != NULL) && (index == 0)) {
				handle->title = config->current->title;
			}
			else {
				handle->title = getTitleByIndex(index);
			}

			if (strstr(pos, "info ") == pos) {
				handle->state = req_current;
			}
			else if (handle->title != NULL) {
				pthread_mutex_lock(&_sendlock);
				handle->state = req_mp3;
			}
			else {
				prepareReply(handle, rep_not_found, true);
			}
		}
		else if (strstr(pos, "/version ") == pos) {
			handle->state = req_version;
		}
		/* HACK to support bookmarklet without HTTPS
		 * todo: remove as soon as HTTPS is supported... */
		else if ((strstr(pos, "/cmd") == pos)
				 && ((mpcmd_t) handle->cmd == mpc_path)) {
			handle->state = req_command;
			cmd = (mpcmd_t) handle->cmd;
		}
		else {
			prepareReply(handle, rep_not_found, true);
		}
		break;
	case met_post:				/* POST */
		if (strstr(pos, "/cmd") == pos) {
			handle->state = req_command;
			cmd = (mpcmd_t) handle->cmd;
			addMessage(MPV + 1, "Got command 0x%04x - %s '%s'",
					   cmd, mpcString(cmd), handle->arg ? handle->arg : "");
		}
		else {					/* unresolvable POST request */
			addMessage(MPV + 0, "Bad POST %s!", pos);
			prepareReply(handle, rep_bad_request, true);
		}
		break;
	case met_file:				/* get file */
		handle->state = req_file;
		if ((strstr(pos, "/ ") == pos) || (strstr(pos, "/index.html ") == pos)) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_index;
		}
		else if ((strstr(pos, "/rc ") == pos) ||
				 (strstr(pos, "/mprc ") == pos) ||
				 (strstr(pos, "/rc.html ") == pos) ||
				 (strstr(pos, "/mprc.html ") == pos)) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mprc;
		}
		else if (strstr(pos, "/mixplay.css ") == pos) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mpcss;
		}
		else if (strstr(pos, "/mixplay.js ") == pos) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mpjs;
		}
		else if ((strstr(pos, "/mixplay.svg ") == pos) ||
				 (strstr(pos, "/favicon.ico ") == pos)) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mpico;
		}
		else if (strstr(pos, "/mixplay.png ") == pos) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mppng;
		}
		else if ((strstr(pos, "/mpplayer.html ") == pos) ||
				 (strstr(pos, "/mpplayer ") == pos)) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mppl;
		}
		else if (strstr(pos, "/mpplayer.js ") == pos) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mppljs;
		}
		else if (strstr(pos, "/manifest.json ") == pos) {
			pthread_mutex_lock(&_sendlock);
			handle->filedef = f_mani;
		}
		else {
			addMessage(MPV + 1, "Illegal get %s", pos);
			prepareReply(handle, rep_not_found, true);
		}
		break;
	case met_upload:
		handle->state = req_none;
		/* parse for the parameters */
		addMessage(MPV + 1, "Upload init");

		if (handle->clientid < 1) {
			/* uploads must not be one-shots! */
			addMessage(0, "No clientID!");
			prepareReply(handle, rep_bad_request, true);
			break;
		}

		// skip the request line
		while (*(pos++) != '\n');
		handle->filesz = 0;
		handle->filerd = 0;
		assert(handle->filefd == -1);
		handle->boundary[0] = '\0';
		char *pline = strcasestr(pos, "Content-Length: ");

		/* content length and boundary are mandatory in this part */
		if (pline != NULL) {
			pline += strlen("Content-Length: ");
			handle->filesz = atoi(pline);
			pline = strcasestr(pos, "boundary=");
			if (pline != NULL) {
				pline = pline + strlen("boundary=");
				for (int i = 0; i < 64; i++) {
					handle->boundary[i] = pline[i];
					if (pline[i] == '\r') {
						handle->boundary[i] = '\0';
						break;
					}
				}
				/* force terminate string */
				handle->boundary[63] = '\0';
				addMessage(MPV + 0, "Init upload: %zu - %s", handle->filesz,
						   handle->boundary);
			}
			else {
				addMessage(MPV + 0, "No boundary found!");
				prepareReply(handle, rep_bad_request, true);
				break;
			}
		}
		else {
			addMessage(MPV + 0, "No content length given!\n%s", pos);
			prepareReply(handle, rep_bad_request, true);
			break;
		}

		if (strcasestr(pos, "filename=\"") == NULL) {
			/* no filename, this will come with the next blob, so skip to 
			 * next block. The request is either needed or will be ignored */
			prepareReply(handle, rep_continue, false);
			break;
		}

		/* fall-through - filename is already in this part */
	case met_datainit:
		/* the first chunk of data */
		handle->state = req_none;
		handle->len = 0;

		char *tline = strcasestr(pos, "filename=\"");

		if (tline != NULL) {
			tline += strlen("filename=\"");
			for (int i = 0; i < FNLEN; i++) {
				handle->fname[i] = tline[i];
				if (tline[i] == '"') {
					handle->fname[i] = '\0';
					break;
				}
			}
			/* force NULL */
			handle->fname[FNLEN - 1] = '\0';
			if ((strlen(handle->fname) == 0)
				|| (!endsWith(handle->fname, ".mp3"))) {
				/* just a simple message as this may be a misclick */
				addMessage(0, "Invalid / no name");
				prepareReply(handle, rep_bad_request, true);
				break;
			}
			tline = strstr(tline, "\r\n\r\n");
		}

		if (tline == NULL) {
			addMessage(-1, "No data found!");
			addMessage(MPV + 0, "%s", handle->commdata);
			prepareReply(handle, rep_bad_request, true);	/* add error */
			break;
		}
		else {
			/* start of the actual data */
			pos = tline + 4;
			/* remove offset to actual file and final boundary marker 
			 * so that filesz is the size of the actual file, not the length
			 * of the file + metadata */
			handle->filesz -=
				(size_t) (pos - handle->commdata) + strlen(handle->boundary) +
				8;
		}

		if (snprintf
			(handle->fpath, MAXPATHLEN, "%supload/%s", config->musicdir,
			 handle->fname) == MAXPATHLEN) {
			/* Unlikely but not impossible if anyone tries to trigger
			 * an overflow */
			addMessage(-1, "Path too long!");
			prepareReply(handle, rep_bad_request, true);
		}
		if (access(handle->fpath, F_OK) == 0) {
			addMessage(-1, "%s<br> was already uploaded", handle->fname);
			prepareReply(handle, rep_bad_request, false);	/* add error */
		}
		else if (mp3FileExists(handle->fname)) {
			addMessage(-1, "%s<br>is already in the collection!",
					   handle->fname);
			/* allow upload for debugging */
			if (getDebug() == 0) {
				prepareReply(handle, rep_bad_request, false);	/* add error */
			}
		}

		/* no error has been set, so start it all */
		if (handle->len == 0) {
			handle->filefd = open(handle->fpath, O_CREAT | O_WRONLY, 00644);
			if (handle->filefd == -1) {
				addMessage(-1, "Could not write<br>%s<br>%s", handle->fpath,
						   strerror(errno));
				prepareReply(handle, rep_bad_request, true);	/* add error */
			}
			else {
				addMessage(0, "Uploading: %s", handle->fname);
				setProcess(0);
			}

		}
		/* fall-through */
	case met_dataflow:
		handle->state = req_none;
		handle->len = 0;

		/* pos is at raw data */
		addMessage(MPV + 1, "data / pos: %p / %p = %zu - %zu",
				   (void *) handle->commdata, (void *) pos,
				   pos - handle->commdata, handle->commlen);

		/* can we find the end of the block? 8 = \r\n--(boundary)-- */
		tline =
			handle->commdata + (handle->commlen -
								(strlen(handle->boundary) + 8));
		if (strstr(tline, handle->boundary) == NULL) {
			/* end of block is end of data */
			tline = handle->commdata + handle->commlen;
		}

		/* only write if there is a target
		 * this should not be an issue but a canceled upload may still 
		 * send data to drop */
		if (handle->filefd > 0) {
			ssize_t len = (size_t) (tline - pos);
			ssize_t sent = 0;

			do {
				/* the write should always succeed but better safe than sorry */
				sent +=
					write(handle->filefd, (unsigned char *) (pos + sent),
						  len - sent);
				if (sent == -1) {
					addMessage(-1, "Write error on %i<br>%s", handle->filefd,
							   strerror(errno));
					prepareReply(handle, rep_bad_request, true);
					break;
				}
				else if (sent < len) {
					addMessage(MPV + 0, "partial write of %zu", sent);
				}
			} while (sent < len);
			handle->filerd += len;
			setProcess((uint32_t) ((100 * handle->filerd) / handle->filesz));
			addMessage(MPV + 1, "Wrote %zu / %zu", handle->filerd,
					   handle->filesz);
		}
		else {
			addMessage(MPV + 0, "Dummy read");
		}


		if (handle->filerd > handle->filesz) {
			/* Truncate and hope for the best */
			addMessage(-1, "Data is bigger than size!");
			handle->filerd = handle->filesz;
		}
		else if (handle->filesz == handle->filerd) {
			/* all done, cleaning up */
			if (handle->filefd > 0) {
				addMessage(0, "Done");
				close(handle->filefd);
				handle->filefd = -1;
				mptitle_t *newt = addNewPath(handle->fpath);

				if (!isStreamActive()) {
					/* since we're playing from the database, add the new title immediately */
					addToPL(newt, config->current, false);
					notifyChange(MPCOMM_TITLES);
				}
			}
			/* handle should be lost after this, but we clean up though */
			handle->fname[0] = '\0';
			handle->boundary[0] = '\0';

			prepareReply(handle, rep_created, false);
		}
		break;

	default:
		addMessage(MPV, "Unknown method %i!", method);
		handle->running = CL_STP;
		break;
	}							/* switch(method) */
}

static void sendReply(chandle_t * handle) {
	char line[MAXPATHLEN] = "";
	struct stat sbuf;
	mpconfig_t *config = getConfig();

	fileinfo_t served[] = {
		{.fname = "src/mixplay.html",
		 .fdata = static_mixplay_html,
		 .flen = static_mixplay_html_len,
		 .mtype = "text/html; charset=utf-8"},
		{.fname = "src/mprc.html",
		 .fdata = static_mprc_html,
		 .flen = static_mprc_html_len,
		 .mtype = "text/html; charset=utf-8"},
		{.fname = "src/mixplay.css",
		 .fdata = static_mixplay_css,
		 .flen = static_mixplay_css_len,
		 .mtype = "text/css; charset=utf-8"},
		{.fname = "src/mixplay.js",
		 .fdata = static_mixplay_js,
		 .flen = static_mixplay_js_len,
		 .mtype = "application/javascript; charset=utf-8"},
		{.fname = "static/mixplay.svg",
		 .fdata = static_mixplay_svg,
		 .flen = static_mixplay_svg_len,
		 .mtype = "image/svg+xml"},
		{.fname = "src/mpplayer.html",
		 .fdata = static_mpplayer_html,
		 .flen = static_mpplayer_html_len,
		 .mtype = "text/html; charset=utf-8"},
		{.fname = "static/mixplay.png",
		 .fdata = static_mixplay_png,
		 .flen = static_mixplay_png_len,
		 .mtype = "image/png"},
		{.fname = "src/mpplayer.js",
		 .fdata = static_mpplayer_js,
		 .flen = static_mpplayer_js_len,
		 .mtype = "application/javascript; charset=utf-8"},
		{.fname = "static/manifest.json",
		 .fdata = static_manifest_json,
		 .flen = static_manifest_json_len,
		 .mtype = "application/manifest+json; charset=utf-8"}
	};

	memset(handle->commdata, 0, handle->commsize);
	mpcmd_t cmd = mpc_idle;
	fileinfo_t *filedef;

	switch (handle->state) {
	case req_none:
		break;

	case req_stop:
		handle->running = CL_STP;
		break;

	case req_update:			/* get update */
		/* add flags that have been set outside */
		handle->fullstat |= getNotify(handle->clientid);
		clearNotify(handle->clientid);

		char *jsonLine = serializeStatus(handle->clientid, handle->fullstat);

		if (jsonLine != NULL) {
			sprintf(handle->commdata,
					"HTTP/1.0 200 OK\015\012"
					"Content-Type: application/json; charset=utf-8\015\012"
					"Content-Length: %i\015\012\015\012",
					(int32_t) strlen(jsonLine));
			while ((ssize_t) (strlen(jsonLine) + strlen(handle->commdata) + 8)
				   > handle->commsize) {
				handle->commsize += MP_BLKSIZE;
				handle->commdata =
					(char *) frealloc(handle->commdata, handle->commsize);
			}
			strcat(handle->commdata, jsonLine);
			strcat(handle->commdata, "\015\012");
			handle->len = strlen(handle->commdata);
			sfree(&jsonLine);
			/* do we still need to send search results? */
			if (deadClient(config->found->cid) ||
				config->found->cid == -1) {
				config->found->cid = 0;
				handle->fullstat &= ~MPCOMM_STAT;
			}
		}
		else {
			addMessage(MPV, "Could not turn status into JSON");
			prepareReply(handle, rep_unavailable, true);	/* add error */
		}
		break;

	case req_command:			/* set command */
		cmd = MPC_CMD(handle->cmd);
		if (cmd < mpc_idle) {
			setCommand(handle->cmd, handle->arg, handle->clientid);
			sfree(&(handle->arg));
			prepareReply(handle, rep_ok, false);
		}
		else {
			prepareReply(handle, rep_not_implemented, false);
		}
		break;

	case req_file:				/* send file */
		filedef = &served[handle->filedef];
		if (getDebug() && (stat(filedef->fname, &sbuf) == 0)) {
			size_t flen = sbuf.st_size;

			sprintf(handle->commdata,
					"HTTP/1.0 200 OK\015\012"
					"Content-Type: %s\015\012"
					"Content-Length: %zu\015\012\015\012",
					filedef->mtype, flen);
			sendloop(handle->sock, handle->commdata, strlen(handle->commdata));
			filePost(handle->sock, filedef->fname);
		}
		else {
			handle->len = 0;
			sprintf(handle->commdata,
					"HTTP/1.0 200 OK\015\012"
					"Content-Type: %s\015\012"
					"Content-Length: %zu\015\012\015\012",
					filedef->mtype, filedef->flen);

			sendloop(handle->sock, handle->commdata, strlen(handle->commdata));
			sendloop(handle->sock, filedef->fdata, filedef->flen);
		}
		pthread_mutex_unlock(&_sendlock);
		handle->len = 0;
		break;

	case req_config:			/* get config should be unreachable */
		addMessage(-1, "Get config is deprecated!");
		prepareReply(handle, rep_unavailable, true);
		break;

	case req_version:			/* get current build version */
		sprintf(handle->commdata,
				"HTTP/1.0 200 OK\015\012"
				"Content-Type: text/plain; charset=utf-8\015\012"
				"Content-Length: %i\015\012\015\012%s",
				(int32_t) strlen(VERSION), VERSION);
		handle->len = strlen(handle->commdata);
		break;

	case req_mp3:				/* send mp3 */
		if (stat(fullpath(handle->title->path), &sbuf) == -1) {
			addMessage(0, "Could not stat %s", fullpath(handle->title->path));
			break;
		}
		/* remove anything non-ascii7bit from the filename so asian
		 * smartphones don't consider the filename to be hanzi */
		sprintf(handle->commdata,
				"HTTP/1.0 200 OK\015\012Content-Type: audio/mpeg\015\012"
				"Content-Length: %ld\015\012"
				"Content-Disposition: attachment; filename=\"%s.mp3\""
				"\015\012\015\012", sbuf.st_size, handle->title->display);
		sendloop(handle->sock, handle->commdata, strlen(handle->commdata));
		line[0] = 0;
		filePost(handle->sock, fullpath(handle->title->path));
		handle->title = NULL;
		pthread_mutex_unlock(&_sendlock);
		handle->len = 0;
		break;

	case req_current:			/* return "artist - title" line */
		if (handle->title != NULL) {
			snprintf(line, MAXPATHLEN, "%s - %s", handle->title->artist,
					 handle->title->title);
		}
		else {
			snprintf(line, MAXPATHLEN, "<initializing>");
		}
		sprintf(handle->commdata,
				"HTTP/1.0 200 OK\015\012"
				"Content-Type: text/plain; charset=utf-8\015\012"
				"Content-Length: %i\015\012\015\012%s",
				(int32_t) strlen(line), line);
		handle->len = strlen(handle->commdata);
		break;

	default:
		addMessage(MPV, "No req_ set, len=%zu", handle->len);
	}

	/* send prepared reply (if any) */
	if (handle->len > 0) {
		sendloop(handle->sock, handle->commdata, handle->len);
	}
	handle->state = req_none;
}

/**
 * This will handle a connection
 *
 * since the context is HTTP the concept of a 'connection' does not really
 * apply here. To keep the protocol as simple as possible, each client has
 * has its own id and the clientHandler() will act according to this ID, not
 * the socket as this may be shared in unexpected ways. For example this
 * allows two sessions in one browser.
 *
 * Since explicit disconnects are not really a thing in HTTP, we're using a
 * watchdog mechanism to maintain active connections. See triggerClient()
 *
 * When the last handler stops, the last clientid will not be removed until a
 * new client connects. This is not a real issue, as just the id of the last
 * client will be locked but MAXCLIENT-1 others are still available. And after
 * 5*clientnum triggerClient() calls the old client will be marked as dead
 * again. It is not worth trying to clean up that id!
 */
static void clientHandler(int arg) {
	mpconfig_t *config;

	chandle_t handle;

	handle.running = CL_ONE;
	handle.state = req_none;
	handle.title = NULL;
	handle.fullstat = MPCOMM_STAT;
	handle.cmd = 0;
	handle.arg = NULL;
	handle.clientid = 0;
	handle.sock = arg;
	handle.filedef = f_none;
	handle.filesz = 0;
	handle.filerd = 0;
	handle.filefd = -1;
	handle.fname[0] = '\0';
	handle.boundary[0] = '\0';
	handle.len = 0;
	handle.commlen = 0;

	/* commsize needs at least to be large enough to hold the javascript file.
	 * Round that size up to the next multiple of MP_BLOCKSIZE */
	handle.commsize = ROUNDUP(static_mixplay_js_len, MP_BLKSIZE);
	handle.commdata = (char *) falloc(handle.commsize, 1);

	config = getConfig();

	/* this one may terminate all willy nilly */
	pthread_detach(pthread_self());
	addMessage(MPV + 3, "Client handler started");

	/* handle a connection, this may be a one shot or a reusable one */
	do {
		/* fetchRequest */
		if (fetchRequest(&handle)) {
			/* all seems good now parse the request.. */
			if (handle.running != CL_STP) {
				parseRequest(&handle);
			}
		}

		/* send a reply if needed */
		if (handle.running != CL_STP) {
			sendReply(&handle);
		}

		if (config->status == mpc_quit) {
			addMessage(0, "stopping handler");
			handle.running = CL_STP;
		}

		/* keep handle->clientid though! */
		sfree(&(handle.arg));
	} while (handle.running & CL_RUN);

	if (handle.filefd > 0) {
		addMessage(0, "Handler for %s just exited", handle.fname);
		addMessage(0, "We probably ended up with a broken upload!");
		close(handle.filefd);
		handle.filefd = -1;
		setProcess(0);
	}

	addMessage(MPV + 3, "Client handler exited");

	pthread_mutex_unlock(&_sendlock); // TODO: really?
	close(handle.sock);
	sfree(&(handle.commdata));
}

#define NUM_THREADS 10

/**
 * thread pool control structure
 */
typedef struct {
	pthread_cond_t notify;
	pthread_mutex_t mutex;
	bool stop;					// shut down the pool then true
	int fdid;					// parameter index
	int fds[NUM_THREADS];		// parameter space
	int idle;					// housekeeping, how many threads are idle
} poolControl_t;

/**
 * the thread pool worker thread
 */
static void *poolthread(void *arg) {
	poolControl_t *pool = (poolControl_t *) arg;
	int runarg;

	for (;;) {
		/* make sure we are exclusive owners of the pool */
		pthread_mutex_lock(&(pool->mutex));
		pool->idle++;
		/* wait for a notification */
		do {
			pthread_cond_wait(&(pool->notify), &(pool->mutex));
		} while ((pool->fdid == 0) && !pool->stop);

		/* Shutdown everything */
		if (pool->stop) {
			break;
		}

		/* save the argument and clear the pool marker */
		pool->fdid--;
		runarg = pool->fds[pool->fdid];
		pool->idle--;

		/* Let the next one check */
		pthread_mutex_unlock(&(pool->mutex));

		/* Handle client */
		clientHandler(runarg);
	}

	pthread_mutex_unlock(&(pool->mutex));
	pthread_exit(NULL);
	return NULL;
}

/**
 * offers a HTTP connection to the player
 * must be called through startServer()
 */
static void *mpserver(void *arg) {
	int32_t mainsocket = (int32_t) (long) arg;
	mpconfig_t *control = getConfig();
	pthread_t *threadpool;		// the actual pool

	poolControl_t pool;

	memset(&pool, 0, sizeof (poolControl_t));

	blockSigint();

	listen(mainsocket, MAXCLIENT);
	addMessage(MPV + 1, "Listening on port %i", control->port);

	/* redirect stdin/out/err in demon mode */
	if (control->isDaemon) {
		int32_t devnull = open("/dev/null", O_RDWR | O_NOCTTY);

		if (devnull == -1) {
			fail(errno, "Could not open /dev/null!");
		}
		if (dup2(devnull, STDIN_FILENO) == -1 ||
			dup2(devnull, STDOUT_FILENO) == -1 ||
			dup2(devnull, STDERR_FILENO) == -1) {
			fail(errno, "Could not redirect std channels!");
		}
		close(devnull);
	}

	/* enable inUI even when not in daemon mode
	 * even if there is no clienthandler yet, messages should be
	 * queued so the first client will see them. */
	control->inUI = true;

	/* initialize the threadpool */
	threadpool = (pthread_t *) malloc(sizeof (pthread_t) * NUM_THREADS);
	pthread_mutex_init(&(pool.mutex), NULL);
	pthread_cond_init(&(pool.notify), NULL);
	pool.stop = false;

	/* start all the threads */
	for (int i = 0; i < NUM_THREADS; i++) {

		if (pthread_create(&(threadpool[i]), NULL,
						   poolthread, (void *) &pool) != 0) {
			fail(F_FAIL, "Could not create client handler threadpool!");
			return NULL;
		}
		else {
			pthread_setname_np(threadpool[i], "clientpool");
		}
	}

	/* Start main loop */
	struct pollfd pfd;

	pfd.fd = mainsocket;
	pfd.events = POLLIN;

	int lastidle = NUM_THREADS;

	while (control->status != mpc_quit) {
		if (poll(&pfd, 1, 250) > 0) {
			socklen_t dummy = 0;
			int32_t client_sock = accept(mainsocket, NULL, &dummy);

			if (client_sock < 0) {
				addMessage(0, "accept() failed!");
				continue;
			}
			addMessage(MPV + 3, "Connection accepted");

			pthread_mutex_lock(&(pool.mutex));
			if (pool.idle == 0) {
				/* I wonder if this ever happens ... */
				addMessage(0, "Ran out of client threads!");
			}

			pool.fds[pool.fdid] = client_sock;
			pool.fdid++;

			if (pool.idle < lastidle) {
				addMessage(MPV + 0, "%2i threads idling", pool.idle);
				lastidle = pool.idle;
			}
			pthread_cond_signal(&(pool.notify));
			pthread_mutex_unlock(&pool.mutex);
		}
	}

	/* clean up thread pool */
	pthread_mutex_lock(&(pool.mutex));
	pool.stop = true;
	pthread_cond_broadcast(&(pool.notify));
	pthread_mutex_unlock(&(pool.mutex));

	/* wait for everyone to return */
	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threadpool[i], NULL);
	}

	/* free threadpool resources */
	sfree((char **) &threadpool);
	pthread_mutex_destroy(&(pool.mutex));
	pthread_cond_destroy(&(pool.notify));

	addMessage(0, "Server stopped");
	close(mainsocket);

	return NULL;
}

/*
 * starts the server thread
 * this call blocks until the server socket was successfuly bound
 * so that nothing else happens on fail()
 */
int32_t startServer() {
	mpconfig_t *control = getConfig();
	int32_t mainsocket = -1;
	int32_t val = 1;
	char host[MP_HOSTLEN];
	char port[20];
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	struct hostent *host_entry;
	struct in_addr **addr_list;

	snprintf(port, 19, "%d", control->port);

	/* retrieve hostname */
	if (gethostname(host, MP_HOSTLEN - 1) == -1) {
		addMessage(0, "Could not get hostname, using 'localhost'!");
		strcpy(host, "localhost");
	}

	/* Check if the hostname can be resolved
	 * this may succeed even if it's just an /etc/hosts entry but then
	 * we claim the maintainer knows what they are doing... */
	host_entry = gethostbyname(host);
	if (host_entry == NULL) {
		fail(errno, "%s does not resolve to any address!", host);
	}

	/* yes, this is about 4 bytes more than we need but with strings it's always
	 * better to have a buffer */
	size_t bmlen = static_bookmarklet_js_len + strlen(host) + strlen(port);

	control->bookmarklet = calloc(1, bmlen + 1);
	if (control->bookmarklet == NULL) {
		fail(errno, "Out of memory while creating bookmarklet!");
	}

	char *bmtemplate = strdup((const char *) static_bookmarklet_js);

	snprintf(control->bookmarklet, bmlen, bmtemplate, host, port);
	free(bmtemplate);

	/* we have the data so we may as well print it on demand */
	if (getDebug() > 1) {
		addr_list = (struct in_addr **) host_entry->h_addr_list;
		for (int i = 0; addr_list[i] != NULL; i++) {
			addMessage(0, "IP address %d: %s\n", i + 1,
					   inet_ntoa(*addr_list[i]));
		}
	}

	memset(&hints, 0, sizeof (hints));
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;

	if (getaddrinfo(NULL, port, &hints, &result) != 0) {
		addMessage(0, "Could not get addrinfo!");
		return -1;
	};

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		mainsocket = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (mainsocket == -1)
			continue;
		if (setsockopt(mainsocket, SOL_SOCKET, SO_REUSEADDR,
					   &val, sizeof (int32_t)) < 0) {
			addMessage(0, "Could not set SO_REUSEADDR on socket!");
			addError(errno);
		}
		else if (bind(mainsocket, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		close(mainsocket);
		mainsocket = -1;
	}

	freeaddrinfo(result);

	if (mainsocket < 0) {
		addMessage(0, "bind to port %i failed!", control->port);
		addError(errno);
		return -1;
	}
	addMessage(MPV + 1, "bind() done");

	pthread_create(&control->stid, NULL, mpserver, (void *) (long) mainsocket);
	pthread_setname_np(control->stid, "mpserver");

	return 0;
}

#undef MPV
