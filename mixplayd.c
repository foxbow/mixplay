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

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"

/* global control structure */
struct mpcontrol_t *mpcontrol;

int main( int argc, char **argv ) {
    unsigned char	c;
    struct 		mpcontrol_t control;
    int			i;
    int 		db=0;
    pid_t		pid[2];

    mpcontrol=&control;

    memset( mpcontrol, 0, sizeof( struct mpcontrol_t ) );

    control.fade=1;
    control.debug=0;

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

    pthread_create( &(control.rtid), NULL, reader, control );

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
