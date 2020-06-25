/* simple API to send commands to mixplayd and read status informations */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <assert.h>

#include "mpclient.h"
#include "utils.h"

static int  _mpport=MP_PORT;
static char _mphost[16]="127.0.0.1";

/*
 * Print errormessage and exit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail( const int error, const char* msg, ... ) {
	va_list args;
	fprintf( stdout, "\n" );
	printf("mpclient: ");
	va_start( args, msg );
	vfprintf( stdout, msg, args );
	va_end( args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
	}
	exit( error );
}

int setMPPort( int port ) {
	if( ( port < 1025 ) || ( port > 65535 ) ) {
		return -1;
	}
	_mpport=port;
	return 0;
}

int setMPHost( const char *host ) {
	if( strlen(host) > 16 ) {
		return -1;
	}
	strtcpy( _mphost, host, 15);
	return 0;
}

#define RBLKSIZE 512

/* send a get request to the server and return the reply as a string
   which MUST be free'd after use! */
static char *sendRequest( clientInfo* usefd, const char *path ) {
	char *req;
	char *reply=NULL;
	char *rdata=NULL;
	char *pos;
	size_t rlen=0;
	size_t clen=0;
	fd_set fds;
	struct timeval	to;
	clientInfo *ci;

	if( usefd == NULL ) {
		ci=getConnection(0);
		if( ci->fd < 0 ) {
			free(ci);
			return NULL;
		}
	}
	else {
		ci = usefd;
	}

	rlen = 13 + strlen(path) + 3 + 1;
	req = (char *)falloc( 1, rlen );
	if( strstr( path, "cmd" ) == path ) {
		strcpy( req, "POST /mpctrl/" );
	}
	else {
		strcpy( req, "GET /mpctrl/" );
	}
	strtcat( req, path, rlen );
	strtcat( req, " http\015\012", rlen );

	if( send( ci->fd, req, strlen(req), 0 ) == -1 ) {
		free(req);
		if( usefd == NULL ) {
			close(ci->fd);
			free(ci);
		}
		return NULL;
	}
	free(req);

	FD_ZERO( &fds );
	FD_SET( ci->fd, &fds );
	to.tv_sec=1;
	to.tv_usec=0;
	if( select( FD_SETSIZE, &fds, NULL, NULL, &to ) > 0 ) {
		if( FD_ISSET(ci->fd, &fds) ) {
			reply = (char*)falloc(512,1);
			rlen=0;
			while( recv( ci->fd, reply+rlen, 512, MSG_DONTWAIT ) == 512 ) {
				rlen = rlen+512;
				reply = (char*)frealloc( reply, rlen+512 );
			}
			rlen=rlen+512;
			/* force terminate reply */
			reply[rlen-1] = 0;
		}
		else {
			return NULL;
		}
	}
	else {
		/* timeout */
		return NULL;
	}

	if( usefd == NULL ) {
		close(ci->fd);
		free(ci);
	}

	if( strstr( reply, "HTTP/1.1 200" ) == reply ) {
		pos=strstr( reply, "Content-Length:" );
		if( pos != NULL ) {
  		pos += strlen("Content-Length:")+1;
			clen=atoi(pos);

			/* content length larger than received data */
			if( clen > rlen ) {
				free(reply);
				return NULL;
			}

			if( clen != 0 ) {
				/* add terminating NUL */
				clen=clen+1;

				pos = strstr(pos, "\015\012\015\012" );
				if( pos != NULL ) {
					pos+=4;

					rdata=(char*)falloc(1, clen);
					strtcpy( rdata, pos, clen );
				}
			}
		}
	}
	else if( strstr( reply, "HTTP/1.") == reply ) {
		rdata=(char*)falloc(1,4);
		strtcpy(rdata, reply+9, 3);
	}
	else {
		rdata=strdup(reply);
	}
	free(reply);

	return rdata;
}

/* open a connection to the server.
   returns:
	 -1 : No socket available
	 -2 : unable to connect to server
	 on error and the socket on success.
	 also registers an update handler for good measure..
*/
clientInfo *getConnection( int keep ) {
	struct sockaddr_in server;
	int fd;
	clientInfo *ci;
	jsonObject *jo=NULL;

	fd=socket(AF_INET, SOCK_STREAM, 0);
	if( fd == -1 ) {
		return NULL;
	}

	memset( &server, 0, sizeof(server) );
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr(_mphost);
	server.sin_port = htons(_mpport);
	if( connect(fd, (struct sockaddr*)&server, sizeof(server)) == -1 ) {
		close(fd);
		return NULL;
	}

	ci=(clientInfo*)calloc(1, sizeof(clientInfo));
	ci->fd=fd;
	if (keep) {
		ci->clientid=-1;
	} else {
		ci->clientid=0;
	}

	jo=getStatus( ci, 0 );
	if( jo == NULL ) {
		close(fd);
		free(ci);
		return NULL;
	}

	ci->clientid=jsonGetInt(jo, "clientid");
	jo=jsonDiscard(jo);
	return ci;
}

/* send a command to mixplayd.
   returns:
	  1 - success
	  0 - server busy
	 -1 - failure on send
*/
int sendCMD( clientInfo *usefd, mpcmd_t cmd){
	char req[1024];
	char *reply;
	jsonObject *jo=NULL;

	if( cmd == mpc_idle ) {
		return 1;
	}

	jo = jsonAddInt( NULL, "cmd", cmd);
	jsonAddInt( jo, "clientid", usefd->clientid);
	reply = jsonToString(jo);
	jo = jsonDiscard(jo);
	snprintf( req, 1023, "cmd?%s x\015\012", reply );
	free(reply);
	reply=sendRequest(usefd, req);
	if( reply == NULL ) {
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
int getCurrentTitle( char *title, unsigned tlen ) {
	char *line;

	line = sendRequest( NULL, "title/info" );
	if( line == NULL ) {
		return -1;
	}
	strtcpy( title, line, tlen < strlen(line) ? tlen : strlen(line)+1 );
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
jsonObject *getStatus(clientInfo *usefd, int flags) {
	char *reply;
	char req[1024];
	jsonObject *jo=NULL;

	jo=jsonAddInt( NULL, "cmd", flags%15 );
	if (usefd == NULL) {
		jsonAddInt( jo, "clientid", 0 );
	} else {
		jsonAddInt( jo, "clientid", usefd->clientid );
	}
	reply = jsonToString(jo);
	jo = jsonDiscard(jo);
	sprintf(req, "status?%s", reply);
	free(reply);
	reply = sendRequest(usefd, req);

	if ( reply != NULL ) {
		if(strlen(reply) < 4 ) {
			jo=jsonAddInt(NULL, "error", atoi(reply));
		}
		else {
			jo=jsonRead(reply);
			if( usefd && (usefd->clientid == -1)) {
				usefd->clientid = jsonGetInt(jo, "clientid");
			}
		}
		free(reply);
	}
	else {
		jo=jsonAddInt(NULL, "error", -1);
	}

	return jo;
}

/*
 * helperfunction to fetch a title from the given jsonObject tree
 */
int jsonGetTitle( jsonObject *jo, const char *key, mptitle_t *title ) {
	assert( title != NULL );
	if( jsonPeek(jo, key) != json_object ) {
		title->key=0;
		strcpy(title->artist, "Mixplay");
		strcpy(title->album, "" );
		strcpy(title->title, "" );
		strcpy(title->display, "Mixplay");
		title->flags=0;
		strcpy(title->genre, "" );
		return 0;
	}
	else {
		jo=jsonGetObj(jo, key);
		title->key=jsonGetInt(jo, "key");
		jsonStrcpy(title->artist, jo, "artist", NAMELEN);
		jsonStrcpy(title->album,  jo, "album",  NAMELEN);
		jsonStrcpy(title->title,  jo, "title",  NAMELEN);
		title->flags=jsonGetInt(jo, "flags");
		jsonStrcpy(title->genre,  jo, "genre",  NAMELEN);
		strtcpy( title->display, title->artist, MAXPATHLEN-1 );
		strtcat( title->display, " - ", MAXPATHLEN-1 );
		strtcat( title->display, title->title, MAXPATHLEN-1 );
		title->playcount=jsonGetInt(jo, "playcount");
		title->skipcount=jsonGetInt(jo, "skipcount");
		return 1;
	}

	return -1;
}
