/*
 * mpserver.c
 *
 * all that is needed to share player information via HTTP
 *
 *  Created on: 01.05.2018
 *      Author: B.Weber
 */

#include "mpserver.h"
#include "mpcomm.h"
#include "mixplayd_html.h"
#include "mprc_html.h"
#include "mixplayd_js.h"
#include "mixplayd_css.h"
#include "player.h"
#include "config.h"
#include "utils.h"
#include "database.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * send a static file
 */
static int filePost( int sock, const char *fname ) {
	int fd;
	fd=open( fname, O_RDONLY );
	if( fd != -1 ) {
		while ( sendfile( sock, fd, 0, 4096 ) == 4096 );
		close(fd);
		return 0;
	}
	else {
		addMessage( 0, "%s not found!", fname );
	}
	return -1;
}

/**
 * decodes parts in the form of %xx
 * In fact I only expect to see %20 in search strings but who knows..
 * and it WILL certainly break soon too..
 * also considers \n or \r to be line end characters
 */
static char *strdec( char *target, const char *src ) {
	int i,j;
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

/**
 * This will handle connection for each client
 */
static void *clientHandler(void *args ) {
	int sock=*(int*)args;
	size_t len, sent, msglen;
	struct timeval to;
	int running=-1;
	char *commdata=NULL;
	char *jsonLine=NULL;
	fd_set fds;
	mpconfig *config;
	unsigned long clmsg;
	int state=0;
	char *pos, *end, *arg;
	mpcmd cmd=mpc_idle;
	static const char *mtype;
	char playing[MAXPATHLEN]="";
	size_t commsize=MP_BLKSIZE;
	ssize_t retval=0;
	ssize_t recvd=0;
	static const char *fname;
	static const unsigned char *fdata;
	unsigned int flen;
	int fullstat=-1;
	int okreply=-1;
	int rawcmd;
	mptitle *title=NULL;

	commdata=falloc( commsize, sizeof( char ) );

	config = getConfig();
	clmsg = config->msg->count;

	pthread_detach(pthread_self());

	addMessage( 2, "Client handler started" );
	while( running && ( config->status!=mpc_quit ) ) {
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
				commdata=frealloc( commdata, commsize );
				memset( commdata+recvd, 0, MP_BLKSIZE );
			}
			switch( retval ) {
			case -1:
				addMessage( 1, "Read error on socket!\n%s", strerror( errno ) );
				running=0;
				break;
			case 0:
				addMessage( 1, "Client disconnected");
				running=0;
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
					if( end == NULL ) {
						addMessage( 0, "Discarding %s", pos );
					}
					else {
						*(end+1)=0;
						/* has an argument and/or msgcount been set? */
						arg=strchr( pos, '?' );
						if( arg != NULL ) {
							arg++;
						}
						if( strstr( pos, "/status " ) == pos ) {
							state=1;
						}
						else if( strstr( pos, "/cmd/" ) == pos ) {
							addMessage( 1, "received: %s", pos );
							pos+=5;
							rawcmd=readHex(pos,&pos);
							if( rawcmd == -1 ) {
								addMessage( 1, "Illegal command %s", pos );
								cmd=mpc_idle;
							}
							else {
								cmd=rawcmd;
								addMessage( 1, "Got command 0x%04x - %s", cmd, mpcString(cmd) );
							}
							if( arg != NULL ) {
								pos=arg;
								if( config->argument != NULL ) {
									addMessage( 1, "Argument %s becomes %s!", config->argument, pos );
									if( strlen(config->argument) <= strlen(pos) ) {
										config->argument=frealloc( config->argument, strlen(pos)+1 );
									}
								}
								else {
									config->argument=falloc( strlen( pos )+2, sizeof( char ) );
								}
								strdec( config->argument, pos );
							}

							if( MPC_CMD(cmd) == mpc_fav ) {
								fullstat=-1;
							}

							state=2;
						}
						else if( ( strstr( pos, "/ ") == pos ) || ( strstr( pos, "/index.html " ) == pos ) ) {
							fname="static/mixplay.html";
							fdata=static_mixplay_html;
							flen=static_mixplay_html_len;
							mtype="text/html; charset=utf-8";
							state=5;
						}
						else if( ( strstr( pos, "/rc " ) == pos ) || ( strstr( pos, "/rc.html " ) == pos )  ) {
							fname="static/mprc.html";
							fdata=static_mprc_html;
							flen=static_mprc_html_len;
							mtype="text/html; charset=utf-8";
							state=5;

						}
						else if( strstr( pos, "/mixplay.css " ) == pos ) {
							fname="static/mixplay.css";
							fdata=static_mixplay_css;
							flen=static_mixplay_css_len;
							mtype="text/css; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/mixplay.js " ) == pos ) {
							fname="static/mixplay.js";
							fdata=static_mixplay_js;
							flen=static_mixplay_js_len;
							mtype="application/javascript; charset=utf-8";
							state=5;
						}
						else if( strstr( pos, "/title/" ) == pos ) {
							addMessage( 1, "received: %s", pos );
							pos+=7;
							title=getTitleByIndex( atoi( pos ) );
							if( title != NULL ) {
								fname=title->path;
								state=8;
							}
							else {
								send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
								state=0;
								running=0;
							}
						}
						else if( strstr( pos, "/favicon.ico " ) == pos ) {
							/* ignore for now */
							send( sock, "HTTP/1.1 204 No Content\015\012\015\012", 28, 0 );
							state=0;
							running=0;
						}
						else if( strstr( pos, "/config ") == pos ) {
							state=6;
						}
						else if( strstr( pos, "/version " ) == pos ) {
							state=7;
						}
						else {
							addMessage( 0, "Illegal get %s", pos );
							send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
							state=0;
							running=0;
						}
					}
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
				if( strcmp( config->current->title->display, playing ) ) {
					strcpy( playing, config->current->title->display );
					fullstat=-1;
				}

				if( isCurClient( sock ) ) {
					fullstat=-1;
				}
				jsonLine=serializeStatus( &clmsg, sock, fullstat );
				fullstat=0;
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8\015\012Content-Length: %i\015\012\015\012", (int)strlen(jsonLine) );
				while( ( strlen(jsonLine) + strlen(commdata) + 8 ) > commsize ) {
					commsize+=MP_BLKSIZE;
					commdata=frealloc( commdata, commsize );
				}
				strcat( commdata, jsonLine );
				len=strlen(commdata);
				sfree( &jsonLine );
				break;
			case 2: /* set command */
				if( cmd != mpc_idle ) {
					if( ( cmd == mpc_dbinfo ) || ( cmd == mpc_dbclean) ||
							( cmd == mpc_doublets ) ||
							( MPC_CMD(cmd) == mpc_search ) ) {
						if( setCurClient( sock ) == -1 ) {
							addMessage( 1, "%s was blocked!", mpcString(cmd) );
							sprintf( commdata, "HTTP/1.1 503 Service Unavailable\015\012Content-Length: 0\015\012\015\012" );
							len=strlen(commdata);
							break;
						}
						clmsg=config->msg->count;
					}
					setCommand(cmd);
					if( okreply ) {
						sprintf( commdata, "HTTP/1.1 204 No Content\015\012\015\012" );
						len=strlen( commdata );
					}
				}
				else {
					sprintf( commdata, "HTTP/1.1 400 Invalid Command\015\012\015\012" );
					len=strlen( commdata );
				}
				break;
			case 3: /* unknown command */
				sprintf( commdata, "HTTP/1.1 501 Not Implemented\015\012\015\012" );
				len=strlen( commdata );
				break;
			case 5: /* send file */
				if( getDebug() ) {
					sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012\015\012", mtype );
					send(sock , commdata, strlen(commdata), 0);
					filePost( sock, fname );
				}
				else {
					sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: %s;\015\012Content-Length: %i;\015\012\015\012", mtype, flen );
					send(sock , commdata, strlen(commdata), 0);
					len=0;
					while( len < flen ) {
						len+=send( sock, &fdata[len], flen-len, 0 );
					}
				}
				len=0;
				running=0;
				break;
			case 6: /* get config */
				jsonLine=serializeConfig( );
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012", (int)strlen(jsonLine) );
				while( ( strlen(jsonLine) + strlen(commdata) + 8 ) > commsize ) {
					commsize+=MP_BLKSIZE;
					commdata=frealloc( commdata, commsize );
				}
				strcat( commdata, jsonLine );
				len=strlen(commdata);
				sfree( &jsonLine );
				break;
			case 7: /* get current build version */
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: text/plain; charset=utf-8;\015\012Content-Length: %i;\015\012\015\012%s",
						(int)strlen(VERSION), VERSION );
				len=strlen(commdata);
				break;
				/* todo: attachment or inline? */
			case 8: /* send mp3 */
				sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: audio/mpeg;\015\012"
						"Content-Disposition: attachment; filename=\"%s.mp3\"\015\012\015\012", title->display );
				send(sock , commdata, strlen(commdata), 0);
				/* a download request is a one-shot and does not interfere with the 'playing' buffer */
				strncpy( playing, config->musicdir, MAXPATHLEN );
				strncat(playing, title->path, MAXPATHLEN-strlen(playing) );
				filePost( sock, playing );
				title=NULL;
				len=0;
				running=0;
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
			}
		} /* if running & !mpc_start */
	}

	addMessage( 2, "Client handler exited" );
	unlockClient( sock );
	close(sock);
	free( args );
	sfree( &commdata );
	sfree( &jsonLine );

	return NULL;
}

/**
 * offers a HTTP connection to the player
 * this must only be called from initAll() and should probably move
 * into player.c
 */
void *mpserver( void *data ) {
	fd_set				fds;
	struct timeval		to;
	int 		mainsocket ,client_sock ,alen ,*new_sock;
	struct sockaddr_in server , client;
	int 		port=MP_PORT;
	mpconfig	*control;
	int devnull=0;

	control=getConfig( );
	/* server can never be remote */
	control->remote=2;


	mainsocket = socket(AF_INET , SOCK_STREAM , 0);
	if (mainsocket == -1) {
		fail( errno, "Could not create socket");
	}
	addMessage( 1, "Socket created" );

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( port );

	if( bind(mainsocket,(struct sockaddr *)&server , sizeof(server)) < 0) {
		fail( errno, "bind() failed!" );
		return NULL;
	}
	addMessage( 1, "bind() done");

	listen(mainsocket , 3);
	addMessage( 0, "Listening on port %i", port );

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
		if ( devnull > 2) {
			close( devnull );
		}
	}

	/* enable inUI even when not in daemon mode */
	control->inUI=-1;
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
				addMessage(0, "accept() failed!" );
				continue;
			}
			addMessage( 2, "Connection accepted" );

			new_sock = falloc( 1, sizeof(int) );
			*new_sock = client_sock;

			/* todo collect pids?
			 * or better use a threadpool */
			if( pthread_create( &pid, NULL, clientHandler, (void*)new_sock ) < 0) {
				addMessage( 0, "Could not create client handler thread!" );
				continue;
			}
		}
	}
	addMessage( 0, "Dropped out of the main loop" );
	/* todo this may return before the threads are done cleaning up.. */
	sleep(1);
	return NULL;
}
