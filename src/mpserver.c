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
#define CL_RUN 1				// client is running
#define CL_SRC 2				// active search

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
	req_unknown,
	req_noservice,
	req_file,
	req_config,
	req_version,
	req_mp3,
	req_current
} httpstate;

/* the parameter set that needs to be accesses by all parts */
typedef struct {
	int sock;
	char *commdata;
	ssize_t commsize;
	uint32_t running;
	mptitle_t *title;
	httpstate state;
	int32_t fullstat;
	filedefs filedef;
	int32_t cmd;
	char *arg;
	int32_t clientid;
	char *fname;
	size_t len;
} chandle_t;

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
					unlockClient(run + 1);
					addMessage(MPV + 2,
							   "client %i disconnected, %i clients connected",
							   run + 1, _numclients);
				}
			}
		}
	}
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

static size_t serviceUnavailable(char *commdata) {
	sprintf(commdata,
			"HTTP/1.1 503 Service Unavailable\015\012Content-Length: 0\015\012\015\012");
	return strlen(commdata);
}

static void fetchRequest(chandle_t * handle) {
	struct pollfd pfd;

	pfd.fd = handle->sock;
	pfd.events = POLLIN;

	/* Either an error or a timeout */
	while ((handle->running != CL_STP) && (poll(&pfd, 1, 250) <= 0)) {
		int deathcount = 0;

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
				/* timeout, no one was calling for two seconds */
				addMessage(MPV + 2, "Reaping unused clienthandler");
				handle->running = CL_STP;
			}
		}
	}

	/* fetch data if any */
	if (pfd.revents & POLLIN) {
		ssize_t recvd = 0;

		memset(handle->commdata, 0, handle->commsize);
		ssize_t retval;

		do {
			retval =
				recv(handle->sock, handle->commdata + recvd,
					 handle->commsize - recvd, MSG_DONTWAIT);
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
				}
			}
			else if (retval == handle->commsize - recvd) {
				/* read buffer is full, expand and go on */
				recvd = handle->commsize;
				handle->commsize += MP_BLKSIZE;
				handle->commdata =
					(char *) frealloc(handle->commdata, handle->commsize);
				memset(handle->commdata + recvd, 0, MP_BLKSIZE);
			}
			else {
				/* everything fit into the buffer, we're done */
				recvd += retval;
				retval = -1;
			}
		} while (retval != -1);

		/* data available but zero bytes read */
		if (recvd == 0) {
			addMessage(MPV + 1, "Client disconnected");
			handle->running = CL_STP;
		}
	}
	else {
		/* nothing came in */
		addMessage(0, "Wrong channel?");
		handle->running = CL_STP;
	}
}

typedef enum {
	met_error = 0,
	met_get,
	met_post,
	met_file
} method_t;

static void parseRequest(chandle_t * handle) {
	method_t method = met_error;
	mpcmd_t cmd = mpc_idle;
	mpconfig_t *config = getConfig();

	addMessage(MPV + 3, "%s", handle->commdata);
	char *end = strchr(handle->commdata, ' ');
	char *pos = end + 1;

	if (end == NULL) {
		addMessage(MPV + 1, "Malformed HTTP: %s", handle->commdata);
		method = met_error;
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
			addMessage(0, "Unsupported method: %s", handle->commdata);
			method = met_error;
		}
	}

	if (method != met_error) {
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
					addMessage(MPV + 1, "Malformed arguments: %s", arg);
					method = met_error;
				}
			}
		}

		/* new update client request */
		if (handle->clientid == -1) {
			handle->clientid = getFreeClient();
			if (handle->clientid == -1) {
				/* no free clientid - no service */
				handle->state = req_noservice;
				return;
			}
			initMsgCnt(handle->clientid);
			addNotify(handle->clientid, MPCOMM_TITLES);
		}

		/* a valid client came in */
		if (handle->clientid > 0) {
			handle->running |= CL_RUN;
			triggerClient(handle->clientid);
		}

		if (end == NULL) {
			addMessage(MPV + 1, "Malformed request %s", pos);
			method = met_error;
		}
		/* control command */
		else if (strstr(pos, "/mpctrl/")) {
			pos = pos + strlen("/mpctrl");
		}
		/* everything else is treated like a GET <path> */
		else {
			if (method == met_get) {
				method = met_file;
			}
			else {
				addMessage(MPV + 1, "Invalid POST request!");
				method = met_error;
			}
		}
	}

	switch (method) {
	case met_get:				/* GET mpcmd */
		if (strcmp(pos, "/status") == 0) {
			handle->state = req_update;
			/* make sure no one asks for searchresults */
			handle->fullstat |= (handle->cmd & ~MPCOMM_RESULT);
			addMessage(MPV + 1, "Statusrequest: 0x%x", handle->fullstat);
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
				send(handle->sock,
					 "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
				handle->running = CL_STP;
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
			send(handle->sock, "HTTP/1.0 404 Not Found\015\012\015\012",
				 25, 0);
			handle->running = CL_STP;
		}
		break;
	case met_post:				/* POST */
		if (strstr(pos, "/cmd") == pos) {
			handle->state = req_command;
			cmd = (mpcmd_t) handle->cmd;
			addMessage(MPV + 1, "Got command 0x%04x - %s '%s'",
					   cmd, mpcString(cmd), handle->arg ? handle->arg : "");
			/* search is synchronous
			 * This is ugly! This code *should* go into the next
			 * step, but we need the searchresults then already.
			 * Changing (cleaning) this would mean a complete
			 * redesign of the server - which may not be the worst
			 * plan anyways...
			 */
			if (MPC_CMD(cmd) == mpc_search) {
				handle->state = req_update;
				if (setCurClient(handle->clientid) == -1) {
					addMessage(-1, "Server is blocked!");
					handle->len = serviceUnavailable(handle->commdata);
				}
				else if (getConfig()->found->state == mpsearch_idle) {
					setCommand(cmd, handle->arg);
					sfree(&(handle->arg));
					handle->running |= CL_SRC;
				}
				/* this case should not be possible at all! */
				else {
					addMessage(-1, "Already searching!");
					unlockClient(handle->clientid);
					handle->len = serviceUnavailable(handle->commdata);
				}
			}
		}
		else {					/* unresolvable POST request */
			send(handle->sock,
				 "HTTP/1.0 406 Not Acceptable\015\012\015\012", 31, 0);
			handle->running = CL_STP;
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
			send(handle->sock, "HTTP/1.0 404 Not Found\015\012\015\012",
				 26, 0);
			handle->running = CL_STP;
		}
		break;
	case met_error:
		addMessage(MPV + 1, "Illegal method %s", handle->commdata);
		send(handle->sock,
			 "HTTP/1.0 405 Method Not Allowed\015\012\015\012", 35, 0);
		handle->running = CL_STP;
		break;
	default:
		addMessage(0, "Unknown method %i!", method);
		handle->running = CL_STP;
		break;
	}							/* switch(method) */
}

static void sendReply(chandle_t * handle) {
	char *jsonLine = NULL;
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
		handle->len = 0;
		break;

	case req_update:			/* get update */
		/* only look at the search state if this is the searcher */
		if ((handle->running & CL_SRC)
			&& (config->found->state != mpsearch_idle)) {
			handle->fullstat |= MPCOMM_RESULT;
			/* poll until the search is done */
			while (config->found->state == mpsearch_busy) {
				nanosleep(&ts, NULL);
			}
		}
		/* add flags that have been set outside */
		handle->fullstat |= getNotify(handle->clientid);
		clearNotify(handle->clientid);

		jsonLine = serializeStatus(handle->clientid, handle->fullstat);
		if (jsonLine != NULL) {
			sprintf(handle->commdata,
					"HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8\015\012Content-Length: %i\015\012\015\012",
					(int32_t) strlen(jsonLine));
			while ((ssize_t) (strlen(jsonLine) + strlen(handle->commdata) + 8)
				   > handle->commsize) {
				handle->commsize += MP_BLKSIZE;
				handle->commdata =
					(char *) frealloc(handle->commdata, handle->commsize);
			}
			strcat(handle->commdata, jsonLine);
			handle->len = strlen(handle->commdata);
			sfree(&jsonLine);
			/* do not clear result flag! */
			handle->fullstat &= MPCOMM_RESULT;
		}
		else {
			addMessage(0, "Could not turn status into JSON");
			handle->len = serviceUnavailable(handle->commdata);
		}
		break;

	case req_command:			/* set command */
		cmd = MPC_CMD(handle->cmd);
		if (cmd < mpc_idle) {
			/* check commands that lock the reply channel */
			if ((cmd == mpc_dbinfo) || (cmd == mpc_dbclean) ||
				(cmd == mpc_doublets)) {
				if (setCurClient(handle->clientid) == -1) {
					addMessage(MPV + 1, "%s was blocked!", mpcString(cmd));
					handle->len = serviceUnavailable(handle->commdata);
					break;
				}
			}
			setCommand(handle->cmd, handle->arg);
			sfree(&(handle->arg));
		}
		sprintf(handle->commdata, "HTTP/1.1 204 No Content\015\012\015\012");
		handle->len = strlen(handle->commdata);
		break;

	case req_unknown:			/* unknown command */
		sprintf(handle->commdata,
				"HTTP/1.1 501 Not Implemented\015\012\015\012");
		handle->len = strlen(handle->commdata);
		break;

	case req_noservice:		/* service unavailable */
		handle->len = serviceUnavailable(handle->commdata);
		break;

	case req_file:				/* send file */
		filedef = &served[handle->filedef];
		if (getDebug() && (stat(filedef->fname, &sbuf) == 0)) {
			size_t flen = sbuf.st_size;

			sprintf(handle->commdata,
					"HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %zu;\015\012\015\012",
					filedef->mtype, flen);
			send(handle->sock, handle->commdata, strlen(handle->commdata), 0);
			filePost(handle->sock, filedef->fname);
		}
		else {
			sprintf(handle->commdata,
					"HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %zu;\015\012\015\012",
					filedef->mtype, filedef->flen);
			send(handle->sock, handle->commdata, strlen(handle->commdata), 0);
			while (handle->len < filedef->flen) {
				handle->len +=
					send(handle->sock, filedef->fdata + handle->len,
						 filedef->flen - handle->len, 0);
			}
		}
		pthread_mutex_unlock(&_sendlock);
		handle->len = 0;
		handle->running &= ~CL_RUN;
		break;

	case req_config:			/* get config should be unreachable */
		addMessage(-1, "Get config is deprecated!");
		handle->len = serviceUnavailable(handle->commdata);
		break;

	case req_version:			/* get current build version */
		sprintf(handle->commdata,
				"HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
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
				"HTTP/1.1 200 OK\015\012Content-Type: audio/mpeg;\015\012"
				"Content-Length: %ld;\015\012"
				"Content-Disposition: attachment; filename=\"%s.mp3\""
				"\015\012\015\012", sbuf.st_size, handle->title->display);
		send(handle->sock, handle->commdata, strlen(handle->commdata), 0);
		line[0] = 0;
		filePost(handle->sock, fullpath(handle->title->path));
		handle->title = NULL;
		pthread_mutex_unlock(&_sendlock);
		handle->len = 0;
		handle->running &= ~CL_RUN;
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
				"HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
				(int32_t) strlen(line), line);
		handle->len = strlen(handle->commdata);
		break;

	}

	if (handle->len > 0) {
		size_t sent = 0, msglen = 0;

		while (sent < handle->len) {
			msglen =
				send(handle->sock, handle->commdata + sent, handle->len - sent,
					 0);
			if (msglen > 0) {
				sent += msglen;
			}
			else {
				sent = handle->len;
				addMessage(MPV + 1, "send failed");
			}
		}
		if (handle->fullstat & MPCOMM_RESULT) {
			config->found->state = mpsearch_idle;
			if (handle->clientid == 0) {
				addMessage(0, "Search reply goes to one-shot!");
			}
			unlockClient(handle->clientid);
			/* clear result flag */
			handle->fullstat &= ~MPCOMM_RESULT;
			/* done searching */
			handle->running &= ~CL_SRC;
		}
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
static void *clientHandler(void *args) {
	mpconfig_t *config;

	chandle_t handle;

	handle.running = CL_RUN;
	handle.state = req_none;
	handle.title = NULL;
	handle.fullstat = MPCOMM_STAT;
	handle.cmd = 0;
	handle.arg = NULL;
	handle.clientid = 0;
	handle.sock = (int32_t) (long) args;
	handle.filedef = f_none;

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
		fetchRequest(&handle);

		/* all seems good now parse the request.. */
		if (handle.running != CL_STP) {
			parseRequest(&handle);
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

	addMessage(MPV + 3, "Client handler exited");
	if (isCurClient(handle.clientid)) {
		addMessage(MPV + 1, "Unlocking client %i", handle.clientid);
		unlockClient(handle.clientid);
		config->found->state = mpsearch_idle;
	}
	pthread_mutex_unlock(&_sendlock);
	close(handle.sock);
	sfree(&(handle.commdata));

	return NULL;
}

/**
 * offers a HTTP connection to the player
 * must be called through startServer()
 */
static void *mpserver(void *arg) {
	struct pollfd pfd;
	int32_t mainsocket = (int32_t) (long) arg;
	int32_t client_sock;
	socklen_t alen;
	mpconfig_t *control = getConfig();
	int32_t devnull = 0;

	blockSigint();

	listen(mainsocket, MAXCLIENT);
	addMessage(MPV + 1, "Listening on port %i", control->port);

	/* redirect stdin/out/err in demon mode */
	if (control->isDaemon) {
		if ((devnull = open("/dev/null", O_RDWR | O_NOCTTY)) == -1) {
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

	/* Start main loop */
	pfd.fd = mainsocket;
	pfd.events = POLLIN;

	while (control->status != mpc_quit) {
		if (poll(&pfd, 1, 250) > 0) {
			pthread_t pid;

			alen = 0;
			client_sock = accept(mainsocket, NULL, &alen);
			if (client_sock < 0) {
				addMessage(0, "accept() failed!");
				continue;
			}
			addMessage(MPV + 3, "Connection accepted");

			/* todo collect pids?
			 * or better use a threadpool */
			if (pthread_create
				(&pid, NULL, clientHandler, (void *) (long) client_sock) < 0) {
				addMessage(0, "Could not create client handler thread!");
			}
			else {
				pthread_setname_np(pid, "clientHandler");
			}
		}
	}
	addMessage(0, "Server stopped");
	/* todo this may return before the threads are done cleaning up.. */
	sleep(1);					// join()ing the threads would be cleaner!
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

	control->bookmarklet = calloc(bmlen, 1);
	if (control->bookmarklet == NULL) {
		fail(errno, "Out of memory while creating bookmarklet!");
	}
	snprintf(control->bookmarklet, bmlen,
			 (const char *) &static_bookmarklet_js[0], host, port);

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
