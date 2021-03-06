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
#include <pthread.h>

#include "mpserver.h"
#include "mpcomm.h"
#include "player.h"
#include "config.h"
#include "utils.h"
#include "database.h"
#include "json.h"

#include "build/mpplayer_html.h"
#include "build/mpplayer_js.h"
#include "build/mixplayd_html.h"
#include "build/mprc_html.h"
#include "build/mixplayd_js.h"
#include "build/mixplayd_css.h"
#include "build/mixplayd_svg.h"
#include "build/mixplayd_png.h"
#include "build/manifest_json.h"

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

/* lock for fname, flen, fdata and mtype */
static pthread_mutex_t _sendlock = PTHREAD_MUTEX_INITIALIZER;

/**
 * send a static file
 */
static int filePost(int sock, const char *fname) {
	int fd;

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
	unsigned int i, j;
	char buf;
	int state = 0;

	for (i = 0, j = 0; i < strlen(src); i++) {
		switch (state) {
		case 0:
			if (src[i] == '%') {
				state = 1;
			}
			else if ((src[i] == 0x0d) || (src[i] == 0x0a)) {
				target[j] = 0;
			}
			else {
				target[j] = src[i];
				j++;
			}
			break;
		case 1:
			buf = 16 * hexval(src[i]);
			state = 2;
			break;
		case 2:
			buf = buf + hexval(src[i]);
			target[j] = buf;
			j++;
			state = 0;
			break;
		}
	}

	/* cut off trailing blanks */
	j = j - 1;
	while (j > 0 && isblank(target[j])) {
		target[j] = 0;
		j--;
	}
	return target;
}

static int fillReqInfo(mpReqInfo * info, char *line) {
	jsonObject *jo = NULL;
	char *jsonLine = calloc(strlen(line), 1);
	int rc = 0;

	strdec(jsonLine, line);
	addMessage(3, "received request: %s", jsonLine);
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
			if (strlen(info->arg) == 0) {
				sfree(&(info->arg));
			}
		}
		info->clientid = jsonGetInt(jo, "clientid");
		addMessage(3, "cmd: %i", info->cmd);
		addMessage(3, "arg: %s", info->arg ? info->arg : "(NULL)");
		addMessage(3, "cid: %i", info->clientid);
	}
	jsonDiscard(jo);
	return rc;
}

static size_t serviceUnavailable(char *commdata) {
	sprintf(commdata,
			"HTTP/1.1 503 Service Unavailable\015\012Content-Length: 0\015\012\015\012");
	return strlen(commdata);
}

#define CL_RUN 1
#define CL_UPD 2
#define CL_ONE 4
#define CL_SRC 8

/**
 * This will handle connection for each client
 */
static void *clientHandler(void *args) {
	int sock;
	size_t len = 0;
	size_t sent, msglen;
	struct timeval to;
	unsigned running = CL_RUN;
	char *commdata = NULL;
	char *jsonLine = NULL;
	fd_set fds;
	mpconfig_t *config;
	httpstate state = req_none;
	char *pos, *end, *arg;
	mpcmd_t cmd = mpc_idle;
	static const char *mtype;
	char line[MAXPATHLEN] = "";
	ssize_t commsize = MP_BLKSIZE;
	ssize_t retval = 0;
	ssize_t recvd = 0;
	static const char *fname;
	static const unsigned char *fdata;
	unsigned int flen;
	int fullstat = MPCOMM_STAT;
	int index = 0;
	mptitle_t *title = NULL;
	struct stat sbuf;

	/* for search polling */
	struct timespec ts;

	ts.tv_nsec = 250000;
	ts.tv_sec = 0;
	char *manifest = NULL;
	unsigned method = 0;
	mpReqInfo reqInfo = { 0, NULL, 0 };
	int clientid = 0;

	commdata = (char *) falloc(commsize, sizeof (char));
	sock = *(int *) args;
	free(args);

	config = getConfig();

	/* this one may terminate all willy nilly */
	pthread_detach(pthread_self());
	addMessage(3, "Client handler started");
	do {
		FD_ZERO(&fds);
		FD_SET(sock, &fds);

		to.tv_sec = 2;
		to.tv_usec = 0;
		if (select(FD_SETSIZE, &fds, NULL, NULL, &to) < 1) {
			switch (errno) {
			case EINTR:
				addMessage(1, "select(%i): Interrupt", clientid);
				break;
			case EBADF:
				addMessage(1, "select(%i): Dead Socket", clientid);
				running &= ~CL_RUN;
				break;
			case EINVAL:
				addMessage(1, "Invalid fds on %i", clientid);
				running &= ~CL_RUN;
				break;
			case ENOMEM:
				addMessage(1, "select(%i): No memory", clientid);
				running &= ~CL_RUN;
				break;
			default:
				addMessage(1, "Reaping dead connection (%i)", clientid);
				running &= ~CL_RUN;
			}
		}

		if (FD_ISSET(sock, &fds)) {
			memset(commdata, 0, commsize);
			recvd = 0;
			while ((retval =
					recv(sock, commdata + recvd, commsize - recvd,
						 MSG_DONTWAIT)) == commsize - recvd) {
				recvd = commsize;
				commsize += MP_BLKSIZE;
				commdata = (char *) frealloc(commdata, commsize);
				memset(commdata + recvd, 0, MP_BLKSIZE);
			}

			/* data available but zero bytes read */
			if (retval == 0) {
				if (recvd == 0) {
					addMessage(1, "Client disconnected");
				}
				else {
					addMessage(1, "Truncated request (%li): %s",
							   (long) (commsize - recvd), commdata);
				}
				running &= ~CL_RUN;
			}
			/* an error occured, EAGAIN and EWOULDBLOCK are ignored */
			else if ((retval == -1) &&
					 (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
				addMessage(1, "Read error on socket!\n%s", strerror(errno));
				running &= ~CL_RUN;
			}
			/* all seems good.. */
			else {
				method = 0;
				toLower(commdata);
				addMessage(3, "%s", commdata);
				end = strchr(commdata, ' ');
				if (end == NULL) {
					addMessage(1, "Malformed HTTP: %s", commdata);
					method = -1;
				}
				else {
					*end = 0;
					pos = end + 1;
					if (strcmp(commdata, "get") == 0) {
						method = 1;
					}
					else if (strcmp(commdata, "post") == 0) {
						method = 2;
					}
					else {
						addMessage(0, "Unsupported method: %s", commdata);
						method = 0;
					}
				}
				if (method > 0) {
					end = strchr(pos, ' ');
					if (end != NULL) {
						*(end + 1) = 0;
						/* has an argument? */
						arg = strchr(pos, '?');
						if (arg != NULL) {
							*arg = 0;
							arg++;
							if (fillReqInfo(&reqInfo, arg)) {
								addMessage(1, "Malformed arguments: %s", arg);
								method = -1;
							}
						}
					}

					/* known client send a request */
					if (reqInfo.clientid > 0) {
						/* but the handler is free */
						if (clientid == 0) {
							clientid = reqInfo.clientid;
							/* is the original handler still active? */
							if (trylockClient(reqInfo.clientid)) {
								/* No, become new update handler */
								running |= CL_UPD;
								/* no initMsgCnt(clientid); as this may still be correct */
								addNotify(clientid, MPCOMM_TITLES);
								/* After a client reload the clientid/socket alignment is lost */
								addMessage(1, "Recreate Update Handler for %i",
										   reqInfo.clientid);
							}
							else {
								/* treat as one-shot for now */
								running = CL_ONE;
								addMessage(1, "Temporary one-shot for %i",
										   reqInfo.clientid);
							}
						}
						if (clientid != reqInfo.clientid) {
							addMessage(0,
									   "Client mix up, expcted %i and got %i!",
									   clientid, reqInfo.clientid);
						}
					}

					if (reqInfo.clientid == -1) {
						if (clientid == 0) {
							/* a new update client! Good, that one should get status updates too! */
							reqInfo.clientid = getFreeClient();
							if (reqInfo.clientid == -1) {
								/* no client - no service */
								state = req_noservice;
								break;
							}
							running |= CL_UPD;
							clientid = reqInfo.clientid;
							initMsgCnt(clientid);
							addNotify(clientid, MPCOMM_TITLES);
							addMessage(1,
									   "Update Handler for client %i initialized",
									   clientid);
						}
						else {
							/* hopefully just a race condition ... */
							addMessage(1, "Duplicate ID request for client %i",
									   clientid);
							reqInfo.clientid = clientid;
						}
					}

					/* Reload may reset the clientid/socket alignment */
					if ((reqInfo.clientid == 0) && (clientid > 0)) {
						addMessage(1, "One shot for client %i", clientid);
						/* stopping the thread may be a bad idea.. */
						/* running&=~CL_RUN; */
					}

					if (clientid == 0) {
						addMessage(2, "One shot request");
						running = CL_ONE;
					}

					if (end == NULL) {
						addMessage(1, "Malformed request %s", pos);
						method = -1;
					}
					/* control command */
					else if (strstr(pos, "/mpctrl/")) {
						pos = pos + strlen("/mpctrl");
					}
					/* everything else is treated like a GET <path> */
					else {
						method = 3;
					}
				}
				switch (method) {
				case 1:		/* GET mpcmd */
					if (strcmp(pos, "/status") == 0) {
						state = req_update;
						/* make sure no one asks for searchresults */
						fullstat |= (reqInfo.cmd & ~MPCOMM_RESULT);
						addMessage(3, "Statusrequest: %i", fullstat);
					}
					else if (strstr(pos, "/title/") == pos) {
						pos += 7;
						index = atoi(pos);
						if ((config->current != NULL) && (index == 0)) {
							title = config->current->title;
						}
						else {
							title = getTitleByIndex(index);
						}

						if (strstr(pos, "info ") == pos) {
							state = req_current;
						}
						else if (title != NULL) {
							pthread_mutex_lock(&_sendlock);
							fname = title->path;
							state = req_mp3;
						}
						else {
							send(sock,
								 "HTTP/1.0 404 Not Found\015\012\015\012", 25,
								 0);
							state = req_none;
							running &= ~CL_RUN;
						}
					}
					else if (strstr(pos, "/version ") == pos) {
						state = req_version;
					}
					else {
						send(sock, "HTTP/1.0 404 Not Found\015\012\015\012",
							 25, 0);
						state = req_none;
						running &= ~CL_RUN;
					}
					break;
				case 2:		/* POST */
					if (strstr(pos, "/cmd") == pos) {
						state = req_command;
						cmd = (mpcmd_t) reqInfo.cmd;
						addMessage(1, "Got command 0x%04x - %s %s", cmd,
								   mpcString(cmd),
								   reqInfo.arg ? reqInfo.arg : "");
						/* search is synchronous */
						if (MPC_CMD(cmd) == mpc_search) {
							if (setCurClient(clientid) == clientid) {
								/* this client cannot already search! */
								assert(getConfig()->found->state ==
									   mpsearch_idle);
								getConfig()->found->state = mpsearch_busy;
								setCommand(cmd, reqInfo.arg ?
										   strdup(reqInfo.arg) : NULL);
								running |= CL_SRC;
								state = req_update;
							}
							else {
								state = req_noservice;
							}
						}
					}
					else {		/* unresolvable POST request */
						send(sock,
							 "HTTP/1.0 406 Not Acceptable\015\012\015\012", 31,
							 0);
						state = req_none;
						running &= ~CL_RUN;
					}
					break;
				case 3:		/* get file */
					state = req_file;
					if ((strstr(pos, "/ ") == pos) ||
						(strstr(pos, "/index.html ") == pos)) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mixplay.html";
						fdata = static_mixplay_html;
						flen = static_mixplay_html_len;
						mtype = "text/html; charset=utf-8";
					}
					else if ((strstr(pos, "/rc ") == pos) ||
							 (strstr(pos, "/rc.html ") == pos)) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mprc.html";
						fdata = static_mprc_html;
						flen = static_mprc_html_len;
						mtype = "text/html; charset=utf-8";
					}
					else if (strstr(pos, "/mixplay.css ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mixplay.css";
						fdata = static_mixplay_css;
						flen = static_mixplay_css_len;
						mtype = "text/css; charset=utf-8";
					}
					else if (strstr(pos, "/mixplay.js ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mixplay.js";
						fdata = static_mixplay_js;
						flen = static_mixplay_js_len;
						mtype = "application/javascript; charset=utf-8";
					}
					else if (strstr(pos, "/mixplay.svg ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mixplay.svg";
						fdata = static_mixplay_svg;
						flen = static_mixplay_svg_len;
						mtype = "image/svg+xml";
					}
					else if (strstr(pos, "/mixplay.png ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mixplay.png";
						fdata = static_mixplay_png;
						flen = static_mixplay_png_len;
						mtype = "image/png";
					}
					else if (strstr(pos, "/mpplayer.html ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mpplayer.html";
						fdata = static_mpplayer_html;
						flen = static_mpplayer_html_len;
						mtype = "text/html; charset=utf-8";
					}
					else if (strstr(pos, "/mpplayer.js ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/mpplayer.js";
						fdata = static_mpplayer_js;
						flen = static_mpplayer_js_len;
						mtype = "application/javascript; charset=utf-8";
					}
					else if (strstr(pos, "/manifest.json ") == pos) {
						pthread_mutex_lock(&_sendlock);
						fname = "static/manifest.json";
						fdata = static_manifest_json;
						flen = static_manifest_json_len;
						mtype = "application/manifest+json; charset=utf-8";
					}
					else if (strstr(pos, "/favicon.ico ") == pos) {
						/* ignore for now */
						send(sock, "HTTP/1.1 204 No Content\015\012\015\012",
							 28, 0);
						state = req_none;
					}
					else {
						addMessage(1, "Illegal get %s", pos);
						send(sock, "HTTP/1.0 404 Not Found\015\012\015\012",
							 25, 0);
						state = req_none;
						running &= ~CL_RUN;
					}
					break;
				case -1:
					addMessage(1, "Illegal method %s", commdata);
					send(sock,
						 "HTTP/1.0 405 Method Not Allowed\015\012\015\012", 35,
						 0);
					state = req_none;
					running &= ~CL_RUN;
					break;
				case -2:		/* generic file not found */
					send(sock, "HTTP/1.0 404 Not Found\015\012\015\012", 25,
						 0);
					state = req_none;
					running &= ~CL_RUN;
					break;
				default:
					addMessage(0, "Unknown method %i!", method);
					state = req_none;
					break;
				}				/* switch(method) */
			}					/* switch(retval) */
		}						/* if fd_isset */

		if (running) {
			memset(commdata, 0, commsize);
			switch (state) {
			case req_none:
				len = 0;
				break;

			case req_update:	/* get update */
				/* add flags that have been set outside */
				if (getNotify(clientid) != MPCOMM_STAT) {
					addMessage(2, "Notification %i for client %i applied",
							   getNotify(clientid), clientid);
					fullstat |= getNotify(clientid);
					setNotify(clientid, MPCOMM_STAT);
				}
				/* only look at the search state if this is the searcher */
				if ((running & CL_SRC)
					&& (config->found->state != mpsearch_idle)) {
					fullstat |= MPCOMM_RESULT;
					/* wait until the search is done */
					while (config->found->state == mpsearch_busy) {
						nanosleep(&ts, NULL);
					}
				}
				jsonLine = serializeStatus(clientid, fullstat);
				if (jsonLine != NULL) {
					sprintf(commdata,
							"HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8\015\012Content-Length: %i\015\012\015\012",
							(int) strlen(jsonLine));
					while ((ssize_t) (strlen(jsonLine) + strlen(commdata) + 8)
						   > commsize) {
						commsize += MP_BLKSIZE;
						commdata = (char *) frealloc(commdata, commsize);
					}
					strcat(commdata, jsonLine);
					len = strlen(commdata);
					sfree(&jsonLine);
					/* do not clear result flag! */
					fullstat &= MPCOMM_RESULT;
				}
				else {
					addMessage(0, "Could not turn status into JSON");
					len = serviceUnavailable(commdata);
				}
				break;

			case req_command:	/* set command */
				if (MPC_CMD(cmd) < mpc_idle) {
					/* check commands that lock the reply channel */
					if ((cmd == mpc_dbinfo) || (cmd == mpc_dbclean) ||
						(cmd == mpc_doublets)) {
						if (setCurClient(clientid) == -1) {
							addMessage(1, "%s was blocked!", mpcString(cmd));
							len = serviceUnavailable(commdata);
							break;
						}
					}
					setCommand(cmd, reqInfo.arg ? strdup(reqInfo.arg) : NULL);
				}
				sprintf(commdata, "HTTP/1.1 204 No Content\015\012\015\012");
				len = strlen(commdata);
				break;

			case req_unknown:	/* unknown command */
				sprintf(commdata,
						"HTTP/1.1 501 Not Implemented\015\012\015\012");
				len = strlen(commdata);
				break;

			case req_noservice:	/* service unavailable */
				len = serviceUnavailable(commdata);
				break;

			case req_file:		/* send file */
				if (getDebug() && (stat(fname, &sbuf) == 0)) {
					flen = sbuf.st_size;
					sprintf(commdata,
							"HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %i;\015\012\015\012",
							mtype, flen);
					send(sock, commdata, strlen(commdata), 0);
					filePost(sock, fname);
				}
				else {
					sprintf(commdata,
							"HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %i;\015\012\015\012",
							mtype, flen);
					send(sock, commdata, strlen(commdata), 0);
					len = 0;
					while (len < flen) {
						len += send(sock, fdata + len, flen - len, 0);
					}
				}
				pthread_mutex_unlock(&_sendlock);
				len = 0;
				break;

			case req_config:	/* get config should be unreachable */
				addMessage(-1, "Get config is deprecated!");
				len = serviceUnavailable(commdata);
				break;

			case req_version:	/* get current build version */
				sprintf(commdata,
						"HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int) strlen(VERSION), VERSION);
				len = strlen(commdata);
				break;

			case req_mp3:		/* send mp3 */
				sprintf(commdata,
						"HTTP/1.1 200 OK\015\012Content-Type: audio/mpeg;\015\012"
						"Content-Disposition: attachment; filename=\"%s.mp3\"\015\012\015\012",
						title->display);
				send(sock, commdata, strlen(commdata), 0);
				line[0] = 0;
				filePost(sock, fullpath(title->path));
				title = NULL;
				pthread_mutex_unlock(&_sendlock);
				len = 0;
				break;

			case req_current:	/* return "artist - title" line */
				if (title != NULL) {
					snprintf(line, MAXPATHLEN, "%s - %s", title->artist,
							 title->title);
				}
				else {
					snprintf(line, MAXPATHLEN, "<initializing>");
				}
				sprintf(commdata,
						"HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int) strlen(line), line);
				len = strlen(commdata);
				break;

			}
			state = 0;

			if (len > 0) {
				sent = 0;
				while (sent < len) {
					msglen = send(sock, commdata + sent, len - sent, 0);
					if (msglen > 0) {
						sent += msglen;
					}
					else {
						sent = len;
						addMessage(1, "send failed");
					}
				}
				if (fullstat & MPCOMM_RESULT) {
					config->found->state = mpsearch_idle;
					if (clientid == 0) {
						addMessage(0, "Search reply goes to one-shot!");
					}
					unlockClient(clientid);
					/* clear result flag */
					fullstat &= ~MPCOMM_RESULT;
					/* done searching */
					running &= ~CL_SRC;
				}
			}
		}						/* if running */
		if (config->status == mpc_quit) {
			addMessage(0, "stopping handler");
			running &= ~CL_RUN;
		}
		reqInfo.cmd = 0;
		sfree(&(reqInfo.arg));
		reqInfo.clientid = 0;
	} while (running & CL_RUN);

	if (running & CL_UPD) {
		if (clientid > 0) {
			addMessage(1, "Update Handler (client %i) terminates", clientid);
			freeClient(clientid);
		}
		else {
			addMessage(0, "Update Handler for client 0 ?!");
		}
	}
	else if (clientid) {
		addMessage(1, "Updatehandler (client %i) got recycled", clientid);
		freeClient(clientid);
	}

	addMessage(3, "Client handler exited");
	if (isCurClient(clientid)) {
		addMessage(1, "Unlocking client %i", clientid);
		unlockClient(clientid);
	}
	if (running & CL_SRC) {
		config->found->state = mpsearch_idle;
	}
	close(sock);
	sfree(&manifest);
	sfree(&commdata);
	sfree(&jsonLine);

	return NULL;
}

/**
 * offers a HTTP connection to the player
 * must be called through startServer()
 */
static void *mpserver(void *arg) {
	fd_set fds;
	struct timeval to;
	int mainsocket = (int) (long) arg;
	int client_sock, alen, *new_sock;
	struct sockaddr_in client;
	mpconfig_t *control = getConfig();
	int devnull = 0;

	blockSigint();

	listen(mainsocket, 3);
	addMessage(1, "Listening on port %i", control->port);

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

	/* enable inUI even when not in daemon mode */
	control->inUI = 1;
	alen = sizeof (struct sockaddr_in);
	/* Start main loop */
	while (control->status != mpc_quit) {
		FD_ZERO(&fds);
		FD_SET(mainsocket, &fds);
		to.tv_sec = 1;
		to.tv_usec = 0;
		if (select(FD_SETSIZE, &fds, NULL, NULL, &to) > 0) {
			pthread_t pid;

			client_sock =
				accept(mainsocket, (struct sockaddr *) &client,
					   (socklen_t *) & alen);
			if (client_sock < 0) {
				addMessage(0, "accept() failed!");
				continue;
			}
			addMessage(3, "Connection accepted");

			/* free()'d in clientHandler() */
			new_sock = (int *) falloc(1, sizeof (int));
			*new_sock = client_sock;

			/* todo collect pids?
			 * or better use a threadpool */
			if (pthread_create(&pid, NULL, clientHandler, (void *) new_sock) <
				0) {
				addMessage(0, "Could not create client handler thread!");
			}
		}
	}
	addMessage(0, "Server stopped");
	/* todo this may return before the threads are done cleaning up.. */
	sleep(1);
	close(mainsocket);

	return NULL;
}

/*
 * starts the server thread
 * this call blocks until the server socket was successfuly bound
 * so that nothing else happens on fail()
 */
int startServer() {
	mpconfig_t *control = getConfig();
	struct sockaddr_in server;
	int mainsocket = -1;
	int val = 1;

	memset(&server, 0, sizeof (server));

	mainsocket = socket(AF_INET, SOCK_STREAM, 0);
	if (mainsocket == -1) {
		addMessage(0, "Could not create socket");
		addError(errno);
		return -1;
	}
	if (setsockopt(mainsocket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof (int)) <
		0) {
		addMessage(0, "Could not set SO_REUSEADDR on socket!");
		addError(errno);
	}
	addMessage(1, "Socket created");

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(control->port);

	if (bind(mainsocket, (struct sockaddr *) &server, sizeof (server)) < 0) {
		addMessage(0, "bind to port %i failed!", control->port);
		addError(errno);
		close(mainsocket);
		return -1;
	}
	addMessage(1, "bind() done");

	pthread_create(&control->stid, NULL, mpserver, (void *) (long) mainsocket);

	return 0;
}
