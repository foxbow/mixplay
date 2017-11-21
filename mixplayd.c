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

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"

/* global control structure */
struct mpcontrol_t *mpcontrol;
static int _ftrpos=0;

/**
 * show activity roller on console
 * this will only show when the global verbosity is larger than 0
 * spins faster with increased verbosity
 */
void activity( const char *msg, ... ) {
    char roller[5]="|/-\\";
    char text[256]="";
    int pos;

    if( getVerbosity() && ( _ftrpos%( 100/getVerbosity() ) == 0 ) ) {
        pos=( _ftrpos/( 100/getVerbosity() ) )%4;

        va_list args;
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

void printver( int vl, const char *msg, ... ) {
    va_list args;

	if( vl >= getVerbosity() ) {
	    va_start( args, msg );
		vfprintf( stderr, msg, args );
	    va_end( args );
	}
}

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *         F_WARN = print message w/o errno and return
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

    fprintf( stdout, "Press [ENTER]\n" );
    fflush( stdout );
    fflush( stderr );

    while( getc( stdin )!=10 );

    if ( error != 0 ) {
        exit( error );
    }

    return;
}

// #define progressLog( ... )   printver( 0, __VA_ARGS__ )
void progressStart( const char* msg, ... ) {
    va_list args;

    va_start( args, msg );
	vfprintf( stderr, msg, args );
    va_end( args );
}

void progressEnd( const char* msg  ) {
	if( msg != NULL ) {
		fputs( msg, stderr );
	}
	else {
		fprintf( stderr, "END\n");
	}
}

void updateUI( mpconfig *data ) {
	;
}

int main( int argc, char **argv ) {
    unsigned char	c;
    struct 		mpcontrol_t control;
    int			i;
    int 		db=0;
    pid_t		pid[2];

    mpcontrol=&control;

    memset( mpcontrol, 0, sizeof( struct mpcontrol_t ) );

    control.fade=1;

    /* parse command line options */
    /* using unsigned char c to work around getopt quirk on ARM */
    while ( ( c = getopt( argc, argv, "vF" ) ) != 255 ) {
        switch ( c ) {
        case 'v': /* increase debug message level to display in console output */
            incVerbosity();
            break;

        case 'F': /* single channel - disable fading */
        	control.fade=0;
        	break;
        }
    }

    readConfig( &control );

    if ( optind < argc ) {
    	if( 0 == setArgument( &control, argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    pthread_create( &(control.rtid), NULL, reader, &control );

    if( NULL == control.root ) {
        /* Runs as thread to have updates in the UI */
        setProfile( &control );
    }
    else {
    	control.active=0;
        control.dbname[0]=0;
        setCommand( &control, mpc_play );
    }

    /* Start main loop */
    while( control.status != mpc_quit ){
    	;
    }

    printver( 2, "Dropped out of the gtk_main loop\n" );

    pthread_join( control.rtid, NULL );
    for( i=0; i <= control.fade; i++ ) {
    	kill( pid[i], SIGTERM );
    }

    freeConfig( &control );
    dbClose( db );

	return 0;
}
