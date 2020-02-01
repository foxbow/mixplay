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
		addMessage( -1, "%s not found!", fname );
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

static size_t serviceUnavailable( char *commdata ) {
	sprintf( commdata, "HTTP/1.1 503 Service Unavailable\015\012Content-Length: 0\015\012\015\012" );
	return strlen(commdata);
}

/**
 * This will handle connection for each client
 */
static void *clientHandler(void *args ) {
	int sock;
	size_t len=0;
	size_t sent, msglen;
	struct timeval to;
	int running=1;
	char *commdata=NULL;
	char *jsonLine=NULL;
	fd_set fds;
	mpconfig_t *config;
	unsigned long clmsg;
	int state=0;
	char *pos, *end, *arg, *argument;
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
	int okreply=-1;
	int rawcmd;
	int index=0;
	mptitle_t *title=NULL;
	struct stat sbuf;
	/* for search polling */
	struct timespec ts;
	ts.tv_nsec=250000;
	ts.tv_sec=0;
	char *manifest=NULL;

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
		select( FD_SETSIZE, &fds, NULL, NULL, &to );
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
				running&=~1;
				break;
			case 0:
				addMessage( 1, "Client disconnected");
				running&=~1;
				break;
			default:
				toLower( commdata );
				/* don't send empty replies for non-web clients */
				if( strstr( commdata, "xmixplay: 1" ) != NULL ) {
					okreply=0;
				}
				pos=strstr( commdata, "get " );
				if( pos != NULL ) {
					pos=pos+4;
					end=strchr( pos, ' ' );
					if( end != NULL ) {
						*(end+1)=0;
					}
					/* This must not be an else due to the following elses */
					if( end == NULL ) {
						addMessage( 1, "Malformed request %s", pos );
					}
					/* control command */
					else if ( strstr( pos, "/mpctrl/")) {
						pos=pos+strlen("/mpctrl");
						/* has an argument and/or msgcount been set? */
						arg=strchr( pos, '?' );
						if( arg != NULL ) {
							*arg=0;
							arg++;
						}
						if( strstr( pos, "/status " ) == pos ) {
							state=1;
						}
						else if( strcmp( pos, "/status" ) == 0 ) {
							state=1;
							fullstat|=atoi(arg);
							addMessage(2,"Statusrequest: %i", fullstat);
						}
						else if( strstr( pos, "/cmd/" ) == pos ) {
							state = 2;
							addMessage( 2, "received cmd: %s", pos );
							pos+=5;
							rawcmd=strtol( pos, &pos, 16 );
							if( rawcmd == -1 ) {
								addMessage( 1, "Illegal command %s", pos );
								cmd=mpc_idle;
							}
							else {
								cmd=(mpcmd_t)rawcmd;
								addMessage( 1, "Got command 0x%04x - %s", cmd, mpcString(cmd) );
							}
							if( arg != NULL ) {
								pos=arg;
								argument=(char*)falloc( strlen( pos )+2, sizeof( char ) );
								strdec( argument, pos );
								addMessage( 1, "Decoded arg: %s", argument );
							}
							else {
								argument = NULL;
							}

							/* search is synchronous */
							if( MPC_CMD(cmd) == mpc_search ) {
								if( setCurClient(sock) != -1 ) {
									/* this client cannot already search! */
									assert( getConfig()->found->state == mpsearch_idle );
									getConfig()->found->state=mpsearch_busy;
									setCommand(cmd, argument);
									state=1;
								} else {
									/* No progressEnd() as it never started */
									unlockClient(sock);
									state=4;
								}
							}
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
								running&=~1;
							}
						} else if( strstr( pos, "/version " ) == pos ) {
								state=7;
						}
					} /* /mpctrl prefix end */
					else {
						if( ( strstr( pos, "/ ") == pos ) || ( strstr( pos, "/index.html " ) == pos ) ) {
							pthread_mutex_lock(&_sendlock);
							fname="static/mixplay.html";
							fdata=static_mixplay_html;
							flen=static_mixplay_html_len;
							mtype="text/html; charset=utf-8";
							state=5;
						}
						else if( ( strstr( pos, "/rc " ) == pos ) || ( strstr( pos, "/rc.html " ) == pos )  ) {
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
							running&=~1;
						}
						else {
							addMessage( 0, "Illegal get %s", pos );
							send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
							state=0;
							running&=~1;
						}
					} /* no prefix */
				} /* /get command */
				else {
					addMessage( 0, "Unknown command %s", commdata );
				}
			} /* switch(retval) */
		} /* if fd_isset */

		if( running && ( config->status != mpc_start ) ) {
			memset( commdata, 0, commsize );
			switch( state ) {
			case 1: /* get update */
				/* an update client! Good, that one should get status updates too! */
				if( running == 1 ) {
					addMessage( 1, "Update Handler (%p/%i) initialized", (void *)&nextstat, sock );
					addNotifyHook( &mps_notify, &nextstat );
					/* a new updater should get the current title too */
					nextstat|=MPCOMM_FULLSTAT;
					running|=2;
				}
				/* add flags that have been set outside */
				if( nextstat != MPCOMM_STAT ) {
					addMessage( 2, "Notification %p/%i applied", (void*)&nextstat, nextstat );
					fullstat |= nextstat;
					nextstat=MPCOMM_STAT;
				}
				if( config->found->state != mpsearch_idle ) {
					fullstat |= MPCOMM_RESULT;
 					while( config->found->state == mpsearch_busy ) {
						nanosleep( &ts, NULL );
					}
				}
				jsonLine=serializeStatus( &clmsg, sock, fullstat );
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
						if( setCurClient( sock ) == -1 ) {
							addMessage( 1, "%s was blocked!", mpcString(cmd) );
							len=serviceUnavailable( commdata );
							break;
						}
						clmsg=config->msg->count;
					}
					setCommand(cmd,argument);
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
				running&=~1;
				break;

			case 6: /* get config should be unreachable */
				addMessage(-1,"Get config is deprecated!");
				len=serviceUnavailable( commdata );
				break;

			case 7: /* get current build version */
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int)strlen(VERSION), VERSION );
				len=strlen(commdata);
				running&=~1;
				break;
				/* todo: attachment or inline? */

			case 8: /* send mp3 */
				pthread_mutex_lock(&_sendlock);
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: audio/mpeg;\015\012"
						"Content-Disposition: attachment; filename=\"%s.mp3\"\015\012\015\012", title->display );
				send(sock , commdata, strlen(commdata), 0);
				line[0]=0;
				filePost( sock, fullpath(title->path) );
				title=NULL;
				len=0;
				running&=~1;
				break;

			case 9: /* return "artist - title" line */
				snprintf( line, MAXPATHLEN, "%s - %s", title->artist, title->title );
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int)strlen(line), line );
				len=strlen(commdata);
				running&=~1;
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
					unlockClient(sock);
					/* clear result flag */
					fullstat&=~MPCOMM_RESULT;
				}
			}

		} /* if running & !mpc_start */
		if( config->status == mpc_quit ) {
			addMessage(0,"stopping handler");
			running&=~1;
		}

	} while( running & 1 );

	if( running & 2 ) {
		removeNotifyHook( &mps_notify, &nextstat );
		addMessage( 1, "Update Handler (%p=%i/%i) terminates", (void *)&nextstat, nextstat, sock );
	}

	addMessage( 2, "Client handler exited" );
	if( isCurClient(sock) ){
		unlockClient( sock );
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
