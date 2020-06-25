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

static pthread_mutex_t _sendlock=PTHREAD_MUTEX_INITIALIZER;

/**
 * send a static file
 */
static int filePost( int sock, const char *fname ) {
	int fd;
	fd=open( fname, O_RDONLY );
	if( fd != -1 ) {
		errno=0;
		while( sendfile( sock, fd, NULL, 4096 ) == 4096 );
		if( errno != 0 ) {
			addMessage( 0, "Error %s sending %s!", strerror(errno), fname );
		}
		close(fd);
	}
	else {
		addMessage( 0, "%s not found!", fname );
	}
	pthread_mutex_unlock(&_sendlock);
	return 0;
}

/**
 * decodes parts in the form of %xx
 * In fact I only expect to see %20 in search strings but who knows..
 * and it WILL certainly break soon too..
 * also considers \n or \r to be line end characters
 */
static char *strdec( char *target, const char *src ) {
	unsigned int i,j;
	char buf;
	int state=0;

	for( i=0, j=0; i<strlen(src); i++ ) {
		switch( state ) {
		case 0:
			if( src[i] == '%' ) {
				state=1;
			}
			else if( ( src[i] == 0x0d ) || ( src[i] == 0x0a ) ) {
				target[j]=0;
			}
			else {
				target[j]=src[i];
				j++;
			}
			break;
		case 1:
			buf=16*hexval(src[i]);
			state=2;
			break;
		case 2:
			buf=buf+hexval(src[i]);
			target[j]=buf;
			j++;
			state=0;
			break;
		}
	}

	/* cut off trailing blanks */
	j=j-1;
	while( j>0 && isblank(target[j]) ) {
		target[j]=0;
		j--;
	}
	return target;
}

/* this is just a dummy function .. */
static void mps_notify( void *arg ) {
	addMessage( 1, "Notification %p/%i", arg, *(int*)arg );
}

static int fillReqInfo( mpReqInfo *info, char *line) {
	jsonObject *jo = NULL;
	char *jsonLine = calloc(strlen(line),1);
	int rc = 0;
	strdec( jsonLine, line );
	addMessage( 2, "received request: %s", jsonLine );
	jo = jsonRead( jsonLine );
	free( jsonLine );
	if( jsonPeek(jo, "cmd") == json_error ) {
		rc = 1;
	}
	else {
		info->cmd=jsonGetInt(jo, "cmd");
		sfree(&(info->arg));
		if( jsonPeek(jo, "arg") == json_string ) {
			info->arg=jsonGetStr(jo, "arg");
			if(strlen(info->arg) == 0) {
				sfree(&(info->arg));
			}
		}
		info->clientid=jsonGetInt(jo, "clientid");
		addMessage(2,"cmd: %i", info->cmd);
		addMessage(2,"arg: %s", info->arg?info->arg:"(NULL)");
		addMessage(2,"cid: %i", info->clientid);
	}
	jsonDiscard(jo);
	return rc;
}

static size_t serviceUnavailable( char *commdata ) {
	sprintf( commdata, "HTTP/1.1 503 Service Unavailable\015\012Content-Length: 0\015\012\015\012" );
	return strlen(commdata);
}

#define CL_RUN 1
#define CL_UPD 2
#define CL_ONE 4
#define CL_SRC 8

/**
 * This will handle connection for each client
 */
static void *clientHandler(void *args ) {
	int sock;
	size_t len=0;
	size_t sent, msglen;
	struct timeval to;
	unsigned running=CL_RUN;
	char *commdata=NULL;
	char *jsonLine=NULL;
	fd_set fds;
	mpconfig_t *config;
	unsigned long clmsg;
	int state=0;
	char *pos, *end, *arg;
	mpcmd_t cmd=mpc_idle;
	static const char *mtype;
	char line[MAXPATHLEN]="";
	ssize_t commsize=MP_BLKSIZE;
	ssize_t retval=0;
	ssize_t recvd=0;
	static const char *fname;
	static const unsigned char *fdata;
	unsigned int flen;
	int fullstat=MPCOMM_STAT;
	int nextstat=MPCOMM_STAT;
	int okreply=1;
	int index=0;
	int tc=0;
	mptitle_t *title=NULL;
	struct stat sbuf;
	/* for search polling */
	struct timespec ts;
	ts.tv_nsec=250000;
	ts.tv_sec=0;
	char *manifest=NULL;
	unsigned method=0;
	mpReqInfo reqInfo = {0, NULL, 0};
	int clientid=0;

	commdata=(char*)falloc( commsize, sizeof( char ) );
	sock=*(int*)args;
	free( args );

	config = getConfig();
	clmsg = msgBufGetLastRead(config->msg);

	/* this one may terminate all willy nilly */
	pthread_detach(pthread_self());
	addMessage( 3, "Client handler started" );
	do {
		FD_ZERO( &fds );
		FD_SET( sock, &fds );

		to.tv_sec=1;
		to.tv_usec=0;
		if( select( FD_SETSIZE, &fds, NULL, NULL, &to ) < 1) {
			/* A client can close a socket but we may not necessarily notice it.
				 So if the select 'fails' ten times in a row, we consider the
				 connection dead and clean it up. Usually this translates to 10
				 seconds of idle, but may also mean 10 signal interruptions. But
				 rather restart a thread once too often than have it unterminated
				 until process death.
				 SO_KEEPALIVE and getsockopt() could probably make sure but both
				 may introduce deadlocks so we're using the dumb approach here */
			if( ++tc > 9 ) {
				addMessage(1, "Reaping dead connection (%i)", sock);
				running&=~CL_RUN;
			}
		}
		else {
			tc=0;
		}
		if( FD_ISSET( sock, &fds ) ) {
			memset( commdata, 0, commsize );
			recvd=0;
			while( ( retval=recv( sock, commdata+recvd, commsize-recvd, 0 ) ) == commsize-recvd ) {
				recvd=commsize;
				commsize+=MP_BLKSIZE;
				commdata=(char*)frealloc( commdata, commsize );
				memset( commdata+recvd, 0, MP_BLKSIZE );
			}
			switch( retval ) {
			case -1:
				addMessage( 1, "Read error on socket!\n%s", strerror( errno ) );
				running&=~CL_RUN;
				break;
			case 0:
				addMessage( 1, "Client disconnected");
				running&=~CL_RUN;
				break;
			default:
				method=0;
				toLower( commdata );
				addMessage(3, "%s", commdata);
				/* don't send empty replies for non-web clients */
				if( strstr( commdata, "xmixplay: 1" ) != NULL ) {
					okreply=0;
				}
				end=strchr( commdata, ' ' );
				if( end == NULL ) {
					addMessage( 1, "Malformed request %s", commdata );
					method=-1;
				}
				else {
					*end=0;
					pos=end+1;
					if( strcmp(commdata, "get") == 0 ) {
						method=1;
					}
					else if( strcmp(commdata, "post") == 0 ) {
						method=2;
					}
					else {
						addMessage(0,"malformed: %s", commdata);
						method=0;
					}
				}
				if( method > 0 ) {
					end=strchr( pos, ' ' );
					if( end != NULL ) {
						*(end+1)=0;
						/* has an argument? */
						arg=strchr( pos, '?' );
						if( arg != NULL ) {
							*arg=0;
							arg++;
							if(fillReqInfo(&reqInfo, arg)) {
								addMessage( 1, "Malformed arguments: %s", arg);
								method=-1;
							}
						}
					}
					// known client send a request
					if(reqInfo.clientid > 0) {
						// but the handler is free
						if (clientid == 0) {
							// is the original handler still active?
							if (trylockClient(reqInfo.clientid) == 0) {
								// yes, one shot request for this client
								running=CL_ONE;
							}
							clientid=reqInfo.clientid;
						}
					}
					if((reqInfo.clientid == 0) && (clientid > 0)){
						reqInfo.clientid=clientid;
					}
					if( (reqInfo.clientid != -1 ) &&
							(clientid != reqInfo.clientid) ) {
						addMessage(0, "Client %i on client %i's handler!?", reqInfo.clientid, clientid);
					}
					/* This must not be an else due to the following elses */
					if( end == NULL ) {
						addMessage( 1, "Malformed request %s", pos );
						method=-1;
					}
					/* control command */
					else if ( strstr( pos, "/mpctrl/")) {
						pos=pos+strlen("/mpctrl");
					}
					/* everything else is treated like a GET <path> */
					else {
						method=3;
					}
				}
				switch(method) {
					case 1: /* GET mpcmd */
						if( strcmp( pos, "/status" ) == 0 ) {
							state=1;
							fullstat|=reqInfo.cmd;
							if( reqInfo.clientid == 0 ) {
								/* one shot */
								running=CL_ONE;
							}
							if( reqInfo.clientid == -1 ) {
								/* a new update client! Good, that one should get status updates too! */
								reqInfo.clientid=getFreeClient();
								addMessage( 1, "Update Handler (%p/%i) initialized", (void *)&nextstat, reqInfo.clientid );
								addNotifyHook( &mps_notify, &nextstat );
								nextstat|=MPCOMM_TITLES;
								running|=CL_UPD;
								clientid=reqInfo.clientid;
							}
							addMessage(2,"Statusrequest: %i", fullstat);
						}
						else if( strstr( pos, "/title/" ) == pos ) {
							pos+=7;
							index=atoi(pos);
							if( ( config->current != NULL ) && ( index == 0 ) ) {
								title=config->current->title;
							}
							else {
								title=getTitleByIndex( index );
							}

							if( strstr( pos, "info " ) == pos ) {
								state=9;
							}
							else if( title != NULL ) {
								pthread_mutex_lock(&_sendlock);
								fname=title->path;
								state=8;
							}
							else {
								send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
								state=0;
								running&=~CL_RUN;
							}
						}
						else if( strstr( pos, "/version " ) == pos ) {
								state=7;
						}
						else {
							send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
							state=0;
							running&=~CL_RUN;
						}
						break;
					case 2: /* POST */
						if( strstr( pos, "/cmd" ) == pos ) {
							state = 2;
							cmd=(mpcmd_t)reqInfo.cmd;
							addMessage( 1, "Got command 0x%04x - %s", cmd, mpcString(cmd) );
							/* search is synchronous */
							if( MPC_CMD(cmd) == mpc_search ) {
								if( setCurClient(clientid) == clientid ) {
									/* this client cannot already search! */
									assert( getConfig()->found->state == mpsearch_idle );
									getConfig()->found->state=mpsearch_busy;
									setCommand(cmd,reqInfo.arg?strdup(reqInfo.arg):NULL);
									running|=CL_SRC;
									state=1;
								} else {
									state=4;
								}
							}
						}
						else { /* unresolvable POST request */
							send(sock , "HTTP/1.0 406 Not Acceptable\015\012\015\012", 31, 0);
							state=0;
							running&=~CL_RUN;
						}
						break;
					case 3: /* get file */
						if( ( strstr( pos, "/ ") == pos ) ||
								( strstr( pos, "/index.html " ) == pos ) ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.html";
							fdata=static_mixplay_html;
							flen=static_mixplay_html_len;
							mtype="text/html; charset=utf-8";
							state=5;
						}
						else if( ( strstr( pos, "/rc " ) == pos ) ||
								( strstr( pos, "/rc.html " ) == pos )  ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mprc.html";
							fdata=static_mprc_html;
							flen=static_mprc_html_len;
							mtype="text/html; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/mixplay.css " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.css";
							fdata=static_mixplay_css;
							flen=static_mixplay_css_len;
							mtype="text/css; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/mixplay.js " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.js";
							fdata=static_mixplay_js;
							flen=static_mixplay_js_len;
							mtype="application/javascript; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/mixplay.svg " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.svg";
							fdata=static_mixplay_svg;
							flen=static_mixplay_svg_len;
							mtype="image/svg+xml";
							state=5;
						}
						else if( strstr( pos, "/mixplay.png " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.png";
							fdata=static_mixplay_png;
							flen=static_mixplay_png_len;
							mtype="image/png";
							state=5;
						}
						else if( strstr( pos, "/mpplayer.html " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mpplayer.html";
							fdata=static_mpplayer_html;
							flen=static_mpplayer_html_len;
							mtype="text/html; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/mpplayer.js " ) == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mpplayer.js";
							fdata=static_mpplayer_js;
							flen=static_mpplayer_js_len;
							mtype="application/javascript; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/manifest.json ") == pos ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/manifest.json";
							fdata=static_manifest_json;
							flen=static_manifest_json_len;
							mtype="application/manifest+json; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/favicon.ico " ) == pos ) {
							/* ignore for now */
							send( sock, "HTTP/1.1 204 No Content\015\012\015\012", 28, 0 );
							state=0;
							running&=~CL_RUN;
						}
						else {
							addMessage( 1, "Illegal get %s", pos );
							send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
							state=0;
							running&=~CL_RUN;
						}
						break;
					case -1:
						addMessage( 1, "Illegal method %s", commdata );
						send(sock , "HTTP/1.0 405 Method Not Allowed\015\012\015\012", 35, 0);
						state=0;
						running&=~CL_RUN;
						break;
					case -2: /* generic file not found */
						send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
						state=0;
						running&=~CL_RUN;
						break;
					default:
						addMessage(0,"Unknown method %i!", method);
						break;
				} /* switch(method) */
			} /* switch(retval) */
		} /* if fd_isset */

		if( running ) {
			memset( commdata, 0, commsize );
			switch( state ) {
			case 1: /* get update */
				/* add flags that have been set outside */
				if( nextstat != MPCOMM_STAT ) {
					addMessage( 2, "Notification %p/%i applied", (void*)&nextstat, nextstat );
					fullstat |= nextstat;
					nextstat=MPCOMM_STAT;
				}
				/* only look at the search state if this is the searcher */
				if( (running&CL_SRC) && (config->found->state != mpsearch_idle) ) {
					fullstat |= MPCOMM_RESULT;
					/* wait until the search is done */
 					while( config->found->state == mpsearch_busy ) {
						nanosleep( &ts, NULL );
					}
				}
				jsonLine=serializeStatus( &clmsg, clientid, fullstat );
				if( jsonLine != NULL ) {
					sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8\015\012Content-Length: %i\015\012\015\012", (int)strlen(jsonLine) );
					while( (ssize_t)( strlen(jsonLine) + strlen(commdata) + 8 ) > commsize ) {
						commsize+=MP_BLKSIZE;
						commdata=(char*)frealloc( commdata, commsize );
					}
					strcat( commdata, jsonLine );
					len=strlen(commdata);
					sfree( &jsonLine );
					/* do not clear result flag! */
					fullstat&=MPCOMM_RESULT;
				}
				else {
					addMessage( 0, "Could not turn status into JSON" );
					len=serviceUnavailable( commdata );
				}
				break;

			case 2: /* set command */
				if(  MPC_CMD(cmd) < mpc_idle ) {
					/* check commands that lock the reply channel */
					if( ( cmd == mpc_dbinfo ) || ( cmd == mpc_dbclean) ||
							( cmd == mpc_doublets ) ){
						if( setCurClient( clientid ) == -1 ) {
							addMessage( 1, "%s was blocked!", mpcString(cmd) );
							len=serviceUnavailable( commdata );
							break;
						}
						clmsg=config->msg->count;
					}
					setCommand(cmd,reqInfo.arg?strdup(reqInfo.arg):NULL);
				}
				if( okreply ) {
					sprintf( commdata, "HTTP/1.1 204 No Content\015\012\015\012" );
					len=strlen( commdata );
				}
				else {
					addMessage(1, "No reply for %i", cmd);
				}
				break;

			case 3: /* unknown command */
				sprintf( commdata, "HTTP/1.1 501 Not Implemented\015\012\015\012" );
				len=strlen( commdata );
				break;

			case 4: /* service unavailable */
				len=serviceUnavailable( commdata );
				break;

			case 5: /* send file */
				if( getDebug() && (stat(fname,&sbuf) == 0 )) {
					flen=sbuf.st_size;
					sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %i;\015\012\015\012", mtype, flen );
					send(sock, commdata, strlen(commdata), 0);
					filePost( sock, fname );
				}
				else {
					sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %i;\015\012\015\012", mtype, flen );
					send(sock , commdata, strlen(commdata), 0);
					len=0;
					while( len < flen ) {
						len+=send( sock, &fdata[len], flen-len, 0 );
					}
					pthread_mutex_unlock(&_sendlock);
				}
				len=0;
				running&=~CL_RUN;
				break;

			case 6: /* get config should be unreachable */
				addMessage(-1,"Get config is deprecated!");
				len=serviceUnavailable( commdata );
				break;

			case 7: /* get current build version */
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int)strlen(VERSION), VERSION );
				len=strlen(commdata);
				running&=~CL_RUN;
				break;
				/* todo: attachment or inline? */

			case 8: /* send mp3 */
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: audio/mpeg;\015\012"
						"Content-Disposition: attachment; filename=\"%s.mp3\"\015\012\015\012", title->display );
				send(sock , commdata, strlen(commdata), 0);
				line[0]=0;
				filePost( sock, fullpath(title->path) );
				title=NULL;
				len=0;
				running&=~CL_RUN;
				break;

			case 9: /* return "artist - title" line */
				snprintf( line, MAXPATHLEN, "%s - %s", title->artist, title->title );
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int)strlen(line), line );
				len=strlen(commdata);
				running&=~CL_RUN;
				break;

			default:
				len=0;
			}
			state=0;

			if( len>0 ) {
				sent=0;
				while( sent < len ) {
					msglen=send(sock , commdata+sent, len-sent, 0);
					if( msglen > 0 ) {
						sent+=msglen;
					}
					else {
						sent=len;
						addMessage( 1, "send failed" );
					}
				}
				if( fullstat & MPCOMM_RESULT ) {
					config->found->state=mpsearch_idle;
					/* not progressEnd() as that sends a confusing 'Done.' and it is
					   certain here that the flow is in the correct context as search
						 is synchronous */
					unlockClient(clientid);
					/* clear result flag */
					fullstat&=~MPCOMM_RESULT;
					/* done searching */
					running&=~CL_SRC;
				}
			}
		} /* if running */
		if( config->status == mpc_quit ) {
			addMessage(0,"stopping handler");
			running&=~CL_RUN;
		}
		reqInfo.cmd=0;
		sfree(&(reqInfo.arg));
		reqInfo.clientid=0;
	} while( running & CL_RUN );

	if( running & CL_UPD ) {
		removeNotifyHook( &mps_notify, &nextstat );
		freeClient(clientid);
		addMessage( 1, "Update Handler (%p=%i/%i) terminates", (void *)&nextstat, nextstat, clientid );
	}

	addMessage( 2, "Client handler exited" );
	if( isCurClient(clientid) ){
		unlockClient(clientid);
	}
	if( running & CL_SRC ) {
		config->found->state = mpsearch_idle;
	}
	close(sock);
	sfree( &manifest );
	sfree( &commdata );
	sfree( &jsonLine );

	return NULL;
}

/**
 * offers a HTTP connection to the player
 * must be called through startServer()
 */
static void *mpserver( void *arg ) {
	fd_set				fds;
	struct timeval		to;
	int	mainsocket = (int)(long)arg;
	int 	client_sock ,alen ,*new_sock;
	struct sockaddr_in client;
	mpconfig_t	*control=getConfig();
	int devnull=0;

	blockSigint();

	listen(mainsocket , 3);
	addMessage( 1, "Listening on port %i", control->port );

	/* redirect stdin/out/err in demon mode */
	if( control->isDaemon ) {
		if ((devnull = open( "/dev/null", O_RDWR | O_NOCTTY)) == -1) {
			fail( errno, "Could not open /dev/null!" );
		}
		if ( dup2( devnull, STDIN_FILENO) == -1 ||
				dup2( devnull, STDOUT_FILENO) == -1 ||
				dup2( devnull, STDERR_FILENO) == -1 ) {
			fail( errno, "Could not redirect std channels!" );
		}
		close( devnull );
	}

	/* enable inUI even when not in daemon mode */
	control->inUI=1;
	alen = sizeof(struct sockaddr_in);
	/* Start main loop */
	while( control->status != mpc_quit ){
		FD_ZERO( &fds );
		FD_SET( mainsocket, &fds );
		to.tv_sec=1;
		to.tv_usec=0;
		if( select( FD_SETSIZE, &fds, NULL, NULL, &to ) > 0 ) {
			pthread_t pid;
			client_sock = accept(mainsocket, (struct sockaddr *)&client, (socklen_t*)&alen);
			if (client_sock < 0) {
				addMessage( 0, "accept() failed!" );
				continue;
			}
			addMessage( 2, "Connection accepted" );

			/* free()'d in clientHandler() */
			new_sock = (int*)falloc( 1, sizeof(int) );
			*new_sock = client_sock;

			/* todo collect pids?
			 * or better use a threadpool */
			if( pthread_create( &pid, NULL, clientHandler, (void*)new_sock ) < 0) {
				addMessage( 0, "Could not create client handler thread!" );
			}
		}
	}
	addMessage( 0, "Server stopped" );
	/* todo this may return before the threads are done cleaning up.. */
	sleep(1);
	close( mainsocket );

	return NULL;
}

/*
 * starts the server thread
 * this call blocks until the server socket was successfuly bound
 * so that nothing else happens on fail()
 */
int startServer( ) {
	mpconfig_t	*control=getConfig( );
	struct sockaddr_in server;
	int mainsocket = -1;
	int rcnt=0;
	memset( &server, 0, sizeof(server) );

	mainsocket = socket(AF_INET , SOCK_STREAM , 0);
	if (mainsocket == -1) {
		addMessage( 0, "Could not create socket");
		addError( errno );
		return -1;
	}
	addMessage( 1, "Socket created" );

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( control->port );

	while ( bind(mainsocket,(struct sockaddr *)&server , sizeof(server)) < 0) {
		if( (control->retry) && (errno == 98) && (rcnt < 4) ) {
			/* this commonly takes a minute so try three times to reconnect if the
			   socket is still blocked, then something is really wrong */
			rcnt++;
			addMessage(0, "Busy, retrying in 20s" );
			sleep(20);
		} else {
			addMessage( 0, "bind to port %i failed!", control->port );
			addError( errno );
			close(mainsocket);
			return -1;
		}
	}
	addMessage( 1, "bind() done");

	pthread_create( &control->stid, NULL, mpserver, (void*)(long)mainsocket );

	return 0;
}
