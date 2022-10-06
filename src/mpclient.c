/* simple API to send commands to mixplayd and read status informations */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <netdb.h>
#include <poll.h>

#include "mpclient.h"
#include "utils.h"

#define MAXHOSTLEN 64
static int32_t _mpport = MP_PORT;
static char _mphost[MAXHOSTLEN + 1] = "localhost";

/*
 * Print errormessage and exit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail(const int32_t error, const char *msg, ...) {
	va_list args;

	fprintf(stdout, "\n");
	printf("mpclient: ");
	va_start(args, msg);
	vfprintf(stdout, msg, args);
	va_end(args);
	fprintf(stdout, "\n");
	if (error > 0) {
		fprintf(stdout, "ERROR: %i - %s\n", abs(error), strerror(abs(error)));
	}
	vsyslog(LOG_ERR, msg, args);
	exit(error);
}

int32_t setMPPort(int32_t port) {
	if ((port < 1025) || (port > 65535)) {
		return -1;
	}
	_mpport = port;
	return 0;
}

/* now works with numerical and symbilic addresses! */
int32_t setMPHost(const char *host) {
	if (strlen(host) > MAXHOSTLEN) {
		return -1;
	}
	strtcpy(_mphost, host, MAXHOSTLEN);
	return 0;
}

const char *getMPHost(void) {
	return _mphost;
}

#define RBLKSIZE 512

/* send a get request to the server and return the reply as a string
   which MUST be free'd after use! */
static char *sendRequest(const char *path) {
	char *req;
	char *reply = NULL;
	char *rdata = NULL;
	char *pos;
	size_t rlen = 0;
	size_t ilen = 0;
	size_t clen = 0;
	int32_t rv = 0;
	struct pollfd pfd;
	int fd = getConnection();

	if (fd < 0) {
		fail(errno, "could not open connection\n");
		return NULL;
	}

	rlen =
		strlen("POST /mpctrl/") + strlen(path) + strlen(" http\015\012") + 1;
	req = (char *) falloc(rlen, 1);
	if (strstr(path, "cmd") == path) {
		strcpy(req, "POST /mpctrl/");
	}
	else {
		strcpy(req, "GET /mpctrl/");
	}
	strtcat(req, path, rlen);
	strtcat(req, " http\015\012", rlen);

	clen = 0;
	do {
		rv = send(fd, req + clen, rlen - clen, 0);
		if (rv < 1) {
			fail(errno, "Could not send request!");
			return NULL;
		}
		clen += rv;
	} while (clen < rlen);
	free(req);

	pfd.fd = fd;
	pfd.events = POLLIN;

	rlen = 0;
	ilen = 0;

	if ((poll(&pfd, 1, 1000) > 0) && (pfd.revents & POLLIN)) {
		do {
			reply = (char *) frealloc(reply, rlen + 512);
			ilen = recv(fd, reply + rlen, 512, MSG_WAITALL);
			if ((ssize_t) ilen == -1) {
				fail(errno, "Read error!");
			}
			rlen = rlen + ilen;
		} while (ilen == 512);
		/* force 0 termination of string */
		reply[rlen] = 0;
	}
	close(fd);

	if (rlen == 0) {
		return NULL;
	}

	if (strstr(reply, "HTTP/1.1 200") == reply) {
		pos = strstr(reply, "Content-Length:");
		if (pos != NULL) {
			pos += strlen("Content-Length:") + 1;
			clen = atoi(pos);

			/* content length larger than received data */
			if (clen > rlen) {
				printf
					("Content length mismatch (expected %zu, received %zu)!\n",
					 clen, rlen);
				printf("%s\n", reply);
			}
			else {
				pos = strstr(pos, "\015\012\015\012");
				if (pos != NULL) {
					pos += 4;
					rdata = strdup(pos);
				}
			}
			free(reply);
		}
	}
	else if (strstr(reply, "HTTP/1.") == reply) {
		/* just copy status number */
		rdata = (char *) falloc(4, 1);
		strtcpy(rdata, reply + 9, 4);
		free(reply);
	}
	else {
		/* raw reply without HTTP header */
		rdata = reply;
	}

	return rdata;
}

/* open a connection to the server.
   returns
	 -1 : No socket available
	 -2 : unable to resolve server
   on error and the connected socket on success.
*/
int getConnection(void) {
	struct addrinfo hint;
	struct addrinfo *result, *ai;
	char port[20];
	int32_t fd = -3;

	memset(&hint, 0, sizeof (hint));
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_STREAM;
	hint.ai_flags = 0;
	hint.ai_protocol = 0;

	snprintf(port, 19, "%d", _mpport);
	if (getaddrinfo(_mphost, port, &hint, &result) != 0) {
		return -2;
	}

	for (ai = result; ai != NULL; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd == -1)
			continue;
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) != -1)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(result);

	return fd;
}

/* send a command to mixplayd.
   returns:
	  1 - success
	  0 - server busy
	 -1 - failure on send
*/
int32_t sendCMD(mpcmd_t cmd, const char *arg) {
	char req[1024];
	char *reply;
	jsonObject *jo = NULL;

	if (cmd == mpc_idle) {
		return 1;
	}

	jo = jsonAddInt(NULL, "cmd", cmd);
	jsonAddStr(jo, "arg", arg);
	jsonAddInt(jo, "clientid", 0);
	reply = jsonToString(jo);
	jo = jsonDiscard(jo);
	snprintf(req, 1023, "cmd?%s x\015\012", reply);
	free(reply);
	reply = sendRequest(req);
	if (reply == NULL) {
		return -1;
	}
	/* we ignore the reply, even if it's an error message */
	free(reply);

	return 1;
}

/*
 * copies the currently pplayed title into the given string and returns
 * the stringlength or -1 on error.
 */
int32_t getCurrentTitle(char *title, uint32_t tlen) {
	char *line;

	line = sendRequest("title/info");
	if (line == NULL) {
		return -1;
	}
	strtcpy(title, line, tlen < strlen(line) ? tlen : strlen(line) + 1);
	free(line);
	return strlen(title);
}

/*
 * fetches the curent player status and returns the reply as a json object
 * flgas is defined by MPCOMM_* in mpcomm.h
 * reply is a jsonObject that either contains the expected data
 * or a single json_integer object with the HTTP status code or
 * -1 on a fatal error
 */
jsonObject *getStatus(int32_t flags) {
	char *reply;
	char req[1024];
	jsonObject *jo = NULL;

	jo = jsonAddInt(NULL, "cmd", flags % 15);
	jsonAddInt(jo, "clientid", 0);
	reply = jsonToString(jo);
	jo = jsonDiscard(jo);
	sprintf(req, "status?%s", reply);
	free(reply);
	reply = sendRequest(req);

	if (reply != NULL) {
		if (strlen(reply) < 4) {
			if (atoi(reply) > 0) {
				jo = jsonAddInt(NULL, "error", atoi(reply));
			}
		}
		else {
			jo = jsonRead(reply);
		}
		free(reply);
	}
	else {
		jo = jsonAddInt(NULL, "error", -1);
	}

	return jo;
}

/*
 * helperfunction to fetch a title from the given jsonObject tree
 */
int32_t jsonGetTitle(jsonObject * jo, const char *key, mptitle_t * title) {
	assert(title != NULL);
	if (jsonPeek(jo, key) != json_object) {
		title->key = 0;
		strcpy(title->artist, "Mixplay");
		strcpy(title->album, "");
		strcpy(title->title, "");
		strcpy(title->display, "Mixplay");
		title->flags = 0;
		strcpy(title->genre, "");
		return 0;
	}
	else {
		jo = jsonGetObj(jo, key);
		title->key = jsonGetInt(jo, "key");
		jsonStrcpy(title->artist, jo, "artist", NAMELEN);
		jsonStrcpy(title->album, jo, "album", NAMELEN);
		jsonStrcpy(title->title, jo, "title", NAMELEN);
		title->flags = jsonGetInt(jo, "flags");
		jsonStrcpy(title->genre, jo, "genre", NAMELEN);
		strtcpy(title->display, title->artist, MAXPATHLEN - 1);
		strtcat(title->display, " - ", MAXPATHLEN - 1);
		strtcat(title->display, title->title, MAXPATHLEN - 1);
		title->playcount = jsonGetInt(jo, "playcount");
		title->skipcount = jsonGetInt(jo, "skipcount");
		return 1;
	}

	return -1;
}
