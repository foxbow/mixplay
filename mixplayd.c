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
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <syslog.h>
#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "mpcomm.h"

static int _ftrpos=0;
static int _isDaemon=1;
/*
 * This will handle connection for each client
 * */
void *clientHandler(void *mainsocket)
{
    int sock = *(int*)mainsocket;
    size_t len;
    struct timeval to;
    int running=1;
    char *commdata;
    fd_set fds;
    mpconfig *config;
    long curmsg;

    commdata=falloc( MP_MAXCOMLEN, sizeof( char ) );
    config = getConfig();
    curmsg = config->msg->count;

    pthread_detach(pthread_self());

    addMessage( 1, "Client handler started" );
    while( running ) {
    	FD_ZERO( &fds );
    	FD_SET( sock, &fds );

    	to.tv_sec=0;
    	to.tv_usec=100000; /* 1/2 second */
    	select( FD_SETSIZE, &fds, NULL, NULL, &to );

    	if( FD_ISSET( sock, &fds ) ) {
			switch( recv(sock , commdata , MP_MAXCOMLEN , 0) ) {
			case -1:
				addMessage( 1, "Read error on socket!\n%s", strerror( errno ) );
				running=0;
				break;
			case 0:
				addMessage( 1, "Client disconnected");
				running=0;
				break;
			default:
				setCommand(mpcCommand(commdata) );
			}
		}

    	if( running && ( config->status != mpc_start ) ) {
			len=serialize( config, commdata, &curmsg );
			if( len != send(sock , commdata , len, 0) ) {
				addMessage( 1, "Send failed!\n%s", strerror( errno ) );
			}
    	}
	}
    addMessage( 1, "Client handler exited" );

    free( mainsocket );
    free( commdata );

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
    if( error <= 0 ) {
        fprintf( stdout, "\n" );
        vfprintf( stdout, msg, args );
        fprintf( stdout, "\n" );
    }
    else {
        fprintf( stdout, "\n" );
        vfprintf( stdout, msg, args );
        fprintf( stdout, "\n ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
    }
    va_end( args );

    exit( error );
}

void progressStart( char* msg, ... ) {
    va_list args;

    va_start( args, msg );
	addMessage( 0, msg );
    va_end( args );
}

void progressEnd( char* msg  ) {
	addMessage( 0, msg );
}

void updateUI( mpconfig *data ) {
	; /* todo log messages */
}

int main( int argc, char **argv ) {
    unsigned char	c;
    mpconfig    *control;
    int			i;
    int 		db=0;
    pid_t		pid[2];
    int 		port=MP_PORT;
    fd_set				fds;
    struct timeval		to;
	int 		mainsocket ,client_sock ,alen ,*new_sock;
	struct sockaddr_in server , client;

    control=readConfig( );
    /* mixplayd can never be remote */
    control->remote=0;
    muteVerbosity();

    /* parse command line options */
    /* using unsigned char c to work around getopt quirk on ARM */
    while ( ( c = getopt( argc, argv, "dFp:D" ) ) != 255 ) {
        switch ( c ) {
        case 'D':
        	_isDaemon=0;
        	break;

        case 'd':
        	incDebug();
        	break;

        case 'v': /* increase debug message level to display in console output */
            incVerbosity();
            break;

        case 'F': /* single channel - disable fading */
        	control->fade=0;
        	break;

        case 'p':
        	port=atoi( optarg );
        	break;
        }
    }

    if( _isDaemon ) {
    	daemon( 0, 0 );
    	openlog ("mixplayd", LOG_PID, LOG_DAEMON);
    }

    if ( optind < argc ) {
    	if( 0 == setArgument( argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    pthread_create( &(control->rtid), NULL, reader, control );

    /* wait for the players to start before handling any commands */
    sleep(1);

    if( NULL == control->root ) {
        /* Runs as thread to have updates in the UI */
        setProfile( control );
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
		return 1;
	}
	addMessage( 1, "bind() done");

	listen(mainsocket , 3);
	addMessage( 0, "Listening on port %i", port );

	alen = sizeof(struct sockaddr_in);
	control->inUI=-1;
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
            addMessage( 1, "Connection accepted" );

            new_sock = falloc( sizeof(int), 1 );
            *new_sock = client_sock;

            /* todo collect pids? */
            if( pthread_create( &pid , NULL ,  clientHandler , (void*) new_sock) < 0) {
                fail( errno, "Could not create thread!" );
                control->status=mpc_quit;
            }
        }
    }
    control->inUI=0;
    if( _isDaemon ) {
    	syslog (LOG_NOTICE, "Dropped out of the main loop");
    }
    addMessage( 1, "Dropped out of the main loop" );

    for( i=0; i <= control->fade; i++ ) {
    	kill( pid[i], SIGTERM );
    }

    freeConfig( );
    dbClose( db );

	return 0;
}
