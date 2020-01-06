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
	printf("mixplay-hid: ");
	va_start( args, msg );
	vfprintf( stdout, msg, args );
	va_end( args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
	}
	exit( error );
}

/* open a connection to the server.
   returns:
	 -1 : No socket available
	 -2 : unable to connect to server
	 on error and the socket on success.
*/
int getConnection() {
	struct sockaddr_in server;
	int fd;

	fd=socket(AF_INET, SOCK_STREAM, 0);
	if( fd == -1 ) {
		return -1;
	}

	memset( &server, 0, sizeof(server) );
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = htons( getConfig()->port );
	if( connect(fd, (struct sockaddr*)&server, sizeof(server)) == -1 ) {
		close(fd);
		return -2;
	}
	return fd;
}

#define RBLKSIZE 512

/* send a get request to the server and return the reply as a string
   which MUST be free'd after use! */
static char *sendRequest( int usefd, const char *path ) {
	char *req;
	char *reply=falloc(1, RBLKSIZE);
	char *rdata=NULL;
	char *pos;
	size_t rlen=0;
	int fd=-1;

	if( usefd == -1 ) {
		fd=getConnection();
		if( fd < 0 ) {
			free(reply);
			return NULL;
		}
	}
	else {
		fd = usefd;
	}

	req = (char *)falloc( 1, 12 + strlen(path) + 3 + 1 );
	strcpy( req, "get /mpctrl/" );
	strtcat( req, path, strlen(path) );
	strtcat( req, " \015\012", 2 );

	if( send( fd, req, strlen(req), 0 ) == -1 ) {
		free(req);
		free(reply);
		if( usefd == -1 ) {
			close(fd);
		}
		return NULL;
	}
	free(req);

	while( recv( fd, reply+rlen, RBLKSIZE, 0 ) == RBLKSIZE ) {
		rlen += RBLKSIZE;
		if( rlen >= 10240 ) {
			free(reply);
			close(fd);
			fail(F_FAIL, "Reply too long!");
			return NULL;
		}
		reply=frealloc(reply, rlen+RBLKSIZE);
		memset(reply+rlen, 0, RBLKSIZE-1);
	}
	if( usefd == -1 ) {
		close(fd);
	}

	/* force terminate reply */
	reply[rlen + RBLKSIZE - 1]=0;

	if( strstr( reply, "HTTP/1.1 200" ) == reply ) {
		pos=strstr( reply, "Content-Length:" );
		if( pos != NULL ) {
  		pos += strlen("Content-Length:")+1;
			rlen=atoi(pos);

			if( rlen >= 10240 ) {
				free(reply);
				fail(F_FAIL, "Illegal Content-length!");
				return NULL;
			}

			if( rlen != 0 ) {
				/* add terminating NUL */
				rlen=rlen+1;

				pos = strstr(pos, "\015\012\015\012" );
				if( pos != NULL ) {
					pos+=4;

					rdata=(char*)falloc(1, rlen);
					strtcpy( rdata, pos, rlen );
				}
			}
		}
	}
	else if( strstr( reply, "HTTP/1.1 ") == reply ) {
		rdata=falloc(1,4);
		strtcpy(rdata, reply+9, 3);
	}
	else {
		rdata=strdup(reply);
	}

	free(reply);
	return rdata;
}

/* send a command to mixplayd.
   returns:
	  1 - success
	  0 - server busy
	 -1 - failure on send
*/
int sendCMD( int usefd, mpcmd_t cmd){
	char req[1024];
	char *reply;

	if( cmd == mpc_idle ) {
		return 1;
	}

	snprintf( req, 1023, "cmd/%04x x\015\012", cmd );
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

	line = sendRequest(-1, "title/info");
	if( line == NULL ) {
		return -1;
	}

	strtcpy( title, line, tlen < strlen(line) ? tlen : strlen(line) );
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
jsonObject *getStatus(int usefd, int flags) {
	char *reply;
	char req[20];
	jsonObject *jo=NULL;

	if (flags) {
		sprintf(req, "status?%i", flags%15);
		reply = sendRequest(usefd, req);
	}
	else {
		reply = sendRequest(usefd, "status");
	}
	if ( reply != NULL ) {
		if(strlen(reply) < 4 ) {
			jo=jsonAddInt(NULL, "error", atoi(reply));
		}
		else {
			jo=jsonRead(reply);
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
		snprintf(title->display, MAXPATHLEN, "%s - %s", title->artist, title->title);
		title->playcount=jsonGetInt(jo, "playcount");
		title->skipcount=jsonGetInt(jo, "skipcount");
		return 1;
	}

	return -1;
}
