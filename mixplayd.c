/*
 * mixplayd.c
 *
 * mixplay demon that play headless and offers a control channel
 * through an IP socket
 *
 *  Created on: 16.11.2017
 *      Author: bweber
 */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <syslog.h>

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "mpcomm.h"
#include "mixplayd_html.h"
#include "mixplayd_js.h"
#include "mixplayd_css.h"

static int _ftrpos=0;
static int _isDaemon=0;
static long _curmsg=0;

/**
 * send a static file
 */
static int filePost( int sock, const char *fname ) {
	int fd;
	fd=open( fname, O_RDONLY );
	if( fd != -1 ) {
		sendfile( sock, fd, 0, 4096 );
		close(fd);
		return 0;
	}
	else {
		addMessage( 0, "%s not found!", fname );
	}
	return -1;
}

/**
 * treats a single character as a hex value
 */
static char hexval( const char c ) {
	if( ( c-'0' >= 0 ) && ( c-'9' <= 9 ) ) {
		return c-'0';
	}

	if( ( c >= 'a') && ( c <= 'f' ) ) {
		return 10+(c-'a');
	}

	addMessage( 0, "Invalid hex character %i - '%c'!", c, c );
	return -1;
}

/**
 * decodes parts in the form of %xx in fact I only expect to see %20
 * in search strings but who knows.. and it WILL break soon too..
 */
static char *strdec( char *target, const char *src ) {
	int i,j;
	char buf;
	int state=0;

	for( i=0, j=0; i<strlen(src); i++ ) {
		switch( state ) {
		case 0:
			if( src[i] != '%' ) {
				target[j]=src[i];
				j++;
			}
			else {
				buf=0;
				state=1;
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
static void *clientHandler(void *args )
{
    int sock=*(int*)args;
    size_t len, sent, msglen;
    struct timeval to;
    int running=-1; /* test */
    char *commdata=NULL;
    char *jsonLine=NULL;
    fd_set fds;
    mpconfig *config;
    long clmsg;
    int state=0;
    char *pos, *end;
    mpcmd cmd=mpc_idle;
    static char *mtype;
    mptitle *playing=NULL;
    size_t commsize=MP_BLKSIZE;
    ssize_t retval=0;
    ssize_t recvd=0;
    const char *fname;
    const unsigned char *fdata;
    unsigned int flen;
    int fullstat=-1;

    commdata=falloc( commsize, sizeof( char ) );

    config = getConfig();
    clmsg = config->msg->count;

    pthread_detach(pthread_self());

    addMessage( 2, "Client handler started" );
    while( running && ( config->status!=mpc_quit ) ) {
    	FD_ZERO( &fds );
    	FD_SET( sock, &fds );

    	to.tv_sec=0;
    	to.tv_usec=100000; /* 1/2 second */
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
					/* legacy entry */
					else if( strstr( pos, "xmixplay: 0" ) == pos ) {
						running=1;
					}
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
								if( strstr( pos+5, "mpc_profile?" ) == pos+5 ) {
									*(pos+16)=0;
									config->active=atoi( pos+17);
								}
								else if( strstr( pos+5, "mpc_search?" ) == pos+5 ) {
									*(pos+15)=0;
									/* this should NEVER happen and is assert() worthy */
									if( config->argument != NULL ) {
										addMessage( 0, "Stray argument: %s!", config->argument );
										sfree( &(config->argument) );
									}
									config->argument=falloc( strlen( pos+16 )+1, sizeof( char ) );
									strdec( config->argument, pos+16 );
								}
								else if( strstr( pos+5, "mpc_setvol?" ) == pos+5 ) {
									*(pos+15)=0;
									config->argument=falloc( strlen( pos+16 )+1, sizeof( char ) );
									strdec( config->argument, pos+16 );
								}
								cmd=mpcCommand(pos+5);
								switch( cmd ) {
								case mpc_favalbum:
								case mpc_favartist:
								case mpc_favtitle:
									fullstat=-1;
									break;
								default:
									break;
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
								pos=0;
							}
							else if( strcmp( pos, "/config") == 0 ) {
								state=6;
							}
							else {
								addMessage( 1, "Illegal get %s", pos );
								send(sock , "HTTP/1.0 404 Not Found\015\012\015\012", 25, 0);
								pos=NULL;
							}
							pos=end+1;
						}
					}
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
				}
			}
		}

    	if( running && ( config->status != mpc_start ) ) {
    		memset( commdata, 0, commsize );
    		switch( state ) {
    		case 11: /* get update */
    			if( config->current->plnext != playing ) {
    				fullstat=-1;
    			}
    			jsonLine=serializeStatus( &clmsg, sock, fullstat );
    			playing=config->current->plnext;
    			fullstat=0;
    			sprintf( commdata, "HTTP/1.1 200 OK\015\012Content-Type: application/json; charset=utf-8\015\012Content-Length: %i;\015\012\015\012", (int)strlen(jsonLine) );
    			while( ( strlen(jsonLine) + strlen(commdata) + 8 ) > commsize ) {
    				commsize+=MP_BLKSIZE;
    				commdata=frealloc( commdata, commsize );
    			}
    			strcat( commdata, jsonLine );
    			strcat( commdata, "\015\012\015\012" );
    			len=strlen(commdata);
    			sfree( &jsonLine );
    			break;
    		case 21: /* set command */
    			if( cmd != mpc_idle ) {
    				sprintf( commdata, "HTTP/1.1 204 No Content\015\012\015\012" );
    				len=strlen( commdata );
    				if( ( cmd == mpc_dbinfo ) || ( cmd == mpc_dbclean) ||
    						( cmd == mpc_doublets ) || ( cmd == mpc_shuffle ) ||
							( cmd == mpc_search ) ) {
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
    			if( _isDaemon == 0 ) {
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
    			state=0;
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
    			strcat( commdata, "\015\012\015\012" );
    			len=strlen(commdata);
    			sfree( &jsonLine );
    			break;
    		default:
    			len=0;
    		}

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
				state=0;
				if( running == 1 ) {
					running=0;
				}
			}
    	}
	}
    addMessage( 2, "Client handler exited" );
   	unlockClient( sock );
	close(sock);
    free( args );
    sfree( &commdata );
    sfree( &jsonLine );

    return 0;
}

/**
 * show activity roller on console
 * this will only show when the global verbosity is larger than 0
 * spins faster with increased verbosity
 */
void activity( const char *msg, ... ) {
    char roller[5]="|/-\\";
    char text[256]="";
    int pos;
    va_list args;

    if( getVerbosity() && ( _ftrpos%( 100/getVerbosity() ) == 0 ) ) {
        pos=( _ftrpos/( 100/getVerbosity() ) )%4;
        va_start( args, msg );
        vsprintf( text, msg, args );
        printf( "%s %c          \r", text, roller[pos] );
        fflush( stdout );
        va_end( args );
    }

    if( getVerbosity() > 0 ) {
        _ftrpos=( _ftrpos+1 )%( 400/getVerbosity() );
    }
}

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *         F_FAIL = print message w/o errno and exit
 */
void fail( int error, const char* msg, ... ) {
    va_list args;
    va_start( args, msg );

    if( _isDaemon ) {
    	vsyslog( LOG_ERR, msg, args );
    }
	fprintf( stdout, "\n" );
	vfprintf( stdout, msg, args );
	fprintf( stdout, "\n" );
    if( error > 0 ) {
        fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
        if( _isDaemon ) {
        	syslog( LOG_ERR, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
        }
    }
    va_end( args );

    exit( error );
}

void progressStart( char* msg, ... ) {
    va_list args;
    char *line;
    line=falloc( 512, sizeof( char ) );

    va_start( args, msg );
    vsnprintf( line, 512, msg, args );
	addMessage( 0, line );
    va_end( args );
}

void progressEnd( ) {
	addMessage( 0, "Done." );
}

void updateUI( ) {
	mpconfig *data=getConfig();
	if( _curmsg < data->msg->count ) {
		if( getDebug() == 0 ) {
			syslog( LOG_NOTICE, "%s", msgBuffPeek( data->msg, _curmsg ) );
		}
		_curmsg++;
	}
}

int main( int argc, char **argv ) {
    mpconfig    *control;
    int 		port=MP_PORT;
    fd_set				fds;
    struct timeval		to;
	int 		mainsocket ,client_sock ,alen ,*new_sock;
	struct sockaddr_in server , client;

    control=readConfig( );
    /* mixplayd can never be remote */
    control->remote=0;
    muteVerbosity();

    switch( getArgs( argc, argv ) ) {
	case 0: /* no arguments given */
		break;

	case 1: /* stream - does this even make sense? */
		break;

	case 2: /* single file */
		break;

	case 3: /* directory */
		/* if no default directory is set, use the one given */
		if( control->musicdir == NULL ) {
			incDebug();
			addMessage( 0, "Setting default configuration values and initializing..." );
			setProfile( control );
			if( control->root == NULL ) {
				fail( F_FAIL, "No music found at %s!", control->musicdir );
			}
			addMessage( 0, "Initialization successful!" );
			writeConfig( argv[optind] );
			freeConfig( );
			return 0;
		}
		break;
	case 4: /* playlist */
		break;
	default:
		fail( F_FAIL, "Unknown argument!\n", argv[optind] );
		return -1;
	}

    /* we are never ever remote */
    control->remote=0;

    /* check if the socket is available before daemon-izing */
	mainsocket = socket(AF_INET , SOCK_STREAM , 0);
	if (mainsocket == -1) {
		fail( errno, "Could not create socket");
	}
	addMessage( 1, "Socket created" );

    if( getDebug() == 0 ) {
    	_isDaemon=-1;
    	daemon( 0, 0 );
    	openlog ("mixplayd", LOG_PID, LOG_DAEMON);
    	/* Make sure that messages end up in the log */
        control->inUI=-1;
    }

   	pthread_create( &(control->rtid), NULL, reader, control );
   	/* wait for the players to start before handling any commands */
   	sleep(1);

    if( NULL == control->root ) {
        setProfile( control );
        if( control->root == NULL ) {
        	fail( F_FAIL, "No music to play!\nStart mixplayd once with a path to set default music directory." );
        }
    }
    else {
    	control->active=0;
        control->dbname[0]=0;
        setPCommand( mpc_play );
    }

    while( control->status != mpc_play ) {
    	if( control->command != mpc_play ) {
    		setCommand( mpc_play );
    		sleep(1);
    	}
    }

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons( port );

	if( bind(mainsocket,(struct sockaddr *)&server , sizeof(server)) < 0) {
		fail( errno, "bind() failed!" );
		return 1;
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
        to.tv_sec=0;
        to.tv_usec=100000; /* 1/10 second */
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
            if( pthread_create( &pid , NULL ,  clientHandler , (void*) new_sock) < 0) {
                fail( errno, "Could not create thread!" );
                control->status=mpc_quit;
            }
        }
    }
    addMessage( 0, "Dropped out of the main loop" );
    control->inUI=0;

    freeConfig( );

	return 0;
}
