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

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "mpcomm.h"


static int _ftrpos=0;

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

    commdata=falloc( MP_MAXCOMLEN, sizeof( char ) );
    config = getConfig();
    pthread_detach(pthread_self());

    addMessage( 1, "Client handler started" );
    config->inUI++;
    while( running ) {
    	FD_ZERO( &fds );
    	FD_SET( sock, &fds );

    	to.tv_sec=0;
    	to.tv_usec=100000; /* 1/2 second */
    	select( FD_SETSIZE, &fds, NULL, NULL, &to );

    	if( FD_ISSET( sock, &fds ) ) {
			switch( recv(sock , commdata , MP_MAXCOMLEN , 0) ) {
			case -1:
				addMessage( 0, "Read error on socket!\n%s", strerror( errno ) );
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
			len=serialize( config, commdata );
			if( len != send(sock , commdata , len, 0) ) {
				addMessage( 0, "Send failed!\n%s", strerror( errno ) );
			}
    	}
	}
    config->inUI--;
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
	;
}

int main( int argc, char **argv ) {
    unsigned char	c;
    mpconfig    *control;
    int			i;
    int 		db=0;
    pid_t		pid[2];
    int 		port=MP_PORT;

    control=readConfig( );
    muteVerbosity();

    /* parse command line options */
    /* using unsigned char c to work around getopt quirk on ARM */
    while ( ( c = getopt( argc, argv, "dvFp:" ) ) != 255 ) {
        switch ( c ) {
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

    if ( optind < argc ) {
    	if( 0 == setArgument( argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    pthread_create( &(control->rtid), NULL, reader, control );

    if( NULL == control->root ) {
        /* Runs as thread to have updates in the UI */
        setProfile( control );
    }
    else {
    	control->active=0;
        control->dbname[0]=0;
        setCommand( mpc_play );
    }

    /* Start main loop */
    while( control->status != mpc_quit ){
        int mainsocket , client_sock , c , *new_sock;
        struct sockaddr_in server , client;

        //Create socket
        mainsocket = socket(AF_INET , SOCK_STREAM , 0);
        if (mainsocket == -1) {
            fail( errno, "Could not create socket");
        }
        addMessage( 1, "Socket created" );

        //Prepare the sockaddr_in structure
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = INADDR_ANY;
        server.sin_port = htons( port );

        //Bind
        if( bind(mainsocket,(struct sockaddr *)&server , sizeof(server)) < 0) {
            fail( errno, "bind() failed!" );
            return 1;
        }
        addMessage( 1, "bind() done");

        //Listen
        listen(mainsocket , 3);

        //Accept and incoming connection
        addMessage( 0, "Listening on port %i", port );

        c = sizeof(struct sockaddr_in);
        while( (client_sock = accept(mainsocket, (struct sockaddr *)&client, (socklen_t*)&c)) ) {
        	pthread_t pid;
            addMessage( 0, "Connection accepted" );

            new_sock = falloc( sizeof(int), 1 );
            *new_sock = client_sock;

            /* todo collect pids */
            if( pthread_create( &pid , NULL ,  clientHandler , (void*) new_sock) < 0) {
                fail( errno, "Could not create thread!" );
                return 1;
            }
        }

        if (client_sock < 0) {
            fail( errno, "accept() failed!" );
        }
    }

    addMessage( 2, "Dropped out of the main loop" );

    pthread_join( control->rtid, NULL );
    for( i=0; i <= control->fade; i++ ) {
    	kill( pid[i], SIGTERM );
    }

    freeConfig( );
    dbClose( db );

	return 0;
}
