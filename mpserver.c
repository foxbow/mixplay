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

#include <arpa/inet.h>
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
		sendfile( sock, fd, 0, 10240 );
		close(fd);
		return 0;
	}
	else {
		addMessage( 0, "%s not found!", fname );
	}
	return -1;
}

/**
 * decodes parts in the form of %xx in fact I only expect to see %20
 * in search strings but who knows.. and it WILL break soon too..
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
	char *pos, *end;
	mpcmd cmd=mpc_idle;
	static const char *mtype;
	char playing[MAXPATHLEN];
	size_t commsize=MP_BLKSIZE;
	ssize_t retval=0;
	ssize_t recvd=0;
	static const char *fname;
	static const unsigned char *fdata;
	unsigned int flen;
	int fullstat=-1;
	int okreply=-1;
	int rawcmd;

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
				pos=commdata;
				while( pos != NULL ) {
					/* empty line means we're done */
					if( ( pos[0]==0x0d ) && ( pos[1]==0x0a ) ) {
						switch(state){
						case 0:
							break;
						default:
							if( state < 10 ) {
								state=(10*state)+1;
							}
						}
						pos=NULL;
					}
					/* don't send empty replies for non-web clients */
					else if( strstr( pos, "xmixplay: 1" ) == pos ) {
						okreply=0;
					}
					/* get command */
					else if( strstr( pos, "get" ) == pos ) {
						pos=pos+4;
						end=strchr( pos, ' ' );
						if( end == NULL ) {
							addMessage( 0, "Discarding %s", pos );
						}
						else {
							*end=0;
							if( strcmp( pos, "/status" ) == 0 ) {
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
								if( *pos == '?' ) {
									pos++;
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
							else if( ( strcmp( pos, "/") == 0 ) || ( strcmp( pos, "/index.html" ) == 0 ) ) {
								fname="static/mixplay.html";
								fdata=static_mixplay_html;
								flen=static_mixplay_html_len;
								mtype="text/html; charset=utf-8";
								state=5;
							}
							else if( ( strcmp( pos, "/rc" ) == 0 ) || ( strcmp( pos, "/rc.html" ) == 0 )  ) {
								fname="static/mprc.html";
								fdata=static_mprc_html;
								flen=static_mprc_html_len;
								mtype="text/html; charset=utf-8";
								state=5;

							}
							else if( strcmp( pos, "/mixplay.css" ) == 0 ) {
								fname="static/mixplay.css";
								fdata=static_mixplay_css;
								flen=static_mixplay_css_len;
								mtype="text/css; charset=utf-8";
								state=5;
							}
							else if( strcmp( pos, "/mixplay.js" ) == 0 ) {
								fname="static/mixplay.js";
								fdata=static_mixplay_js;
								flen=static_mixplay_js_len;
								mtype="application/javascript; charset=utf-8";
								state=5;
							}
							else if( strcmp( pos, "/favicon.ico" ) == 0 ) {
								/* ignore for now */
								send( sock, "HTTP/1.1 204 No Content\015\012\015\012", 28, 0 );
								pos=NULL;
							}
							else if( strcmp( pos, "/config") == 0 ) {
								state=6;
							}
							else {
								addMessage( 1, "Illegal get %s", pos );
								send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
								running=0;
								pos=NULL;
							}
							/* set pos to a sensible position so the next line can be found */
							if( pos != NULL ) {
								pos=end+1;
							}
						}
					} /* /get command */

					/* get next request line */
					if( pos!=NULL ) {
						while( pos[1] != 0 ) {
							if( ( pos[0] == 0x0d ) && ( pos[1] == 0x0a ) ) {
								pos+=2;
								break;
							}
							pos++;
						}
						if( pos[1] == 0 ) {
							pos=NULL;
						}
					}
				} /* while ( pos != NULL ) */
			} /* switch(retval) */
		} /* if fd_isset */

		if( running && ( config->status != mpc_start ) ) {
			memset( commdata, 0, commsize );
			switch( state ) {
			case 11: /* get update */
				if( strcmp( config->current->display, playing ) ) {
					strcpy( playing, config->current->display );
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
			case 21: /* set command */
				if( cmd != mpc_idle ) {
					if( okreply ) {
						sprintf( commdata, "HTTP/1.1 204 No Content\015\012\015\012" );
						len=strlen( commdata );
					}
					if( ( cmd == mpc_dbinfo ) || ( cmd == mpc_dbclean) ||
							( cmd == mpc_doublets ) ||
							( MPC_CMD(cmd) == mpc_search ) ) {
						setCurClient( sock );
						/* setCurClient may block so we need to skip messages */
						clmsg=config->msg->count;
					}
					setCommand(cmd);
				}
				else {
					sprintf( commdata, "HTTP/1.1 400 Invalid Command\015\012\015\012" );
					len=strlen( commdata );
				}
				break;
			case 31: /* unknown command */
				sprintf( commdata, "HTTP/1.1 501 Not Implemented\015\012\015\012" );
				len=strlen( commdata );
				break;
			case 51: /* send file */
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
			case 61: /* get config */
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

	control=readConfig( );
	/* server can never be remote */
	control->remote=0;


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
				fail( errno, "accept() failed!" );
				control->status=mpc_quit;
			}
			addMessage( 2, "Connection accepted" );

			new_sock = falloc( 1, sizeof(int) );
			*new_sock = client_sock;

			/* todo collect pids?
			 * or better use a threadpool */
			if( pthread_create( &pid, NULL, clientHandler, (void*)new_sock ) < 0) {
				fail( errno, "Could not create client handler thread!" );
				control->status=mpc_quit;
			}
		}
	}
	addMessage( 0, "Dropped out of the main loop" );
	return NULL;
}
