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

    buildUI( &control );

    control.root=NULL;
    control.playstream=0;

    if ( optind < argc ) {
    	if( 0 == setArgument( &control, argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    if( control.debug ) {
        progressStart( "Debug" );
    }

    /* start the player processes */
    /* these may wait in the background until */
    /* something needs to be played at all */
    for( i=0; i <= control.fade; i++ ) {
        /* create communication pipes */
        pipe( control.p_status[i] );
        pipe( control.p_command[i] );
        pid[i] = fork();

        if ( 0 > pid[i] ) {
            fail( errno, "could not fork" );
        }

        /* child process */
        if ( 0 == pid[i] ) {
            printver( 2, "Starting player %i\n", i+1 );

            if ( dup2( control.p_command[i][0], STDIN_FILENO ) != STDIN_FILENO ) {
                fail( errno, "Could not dup stdin for player %i", i+1 );
            }

            if ( dup2( control.p_status[i][1], STDOUT_FILENO ) != STDOUT_FILENO ) {
                fail( errno, "Could not dup stdout for player %i", i+1 );
            }

            /* this process needs no pipes */
            close( control.p_command[i][0] );
            close( control.p_command[i][1] );
            close( control.p_status[i][0] );
            close( control.p_status[i][1] );
            /* Start mpg123 in Remote mode */
            execlp( "mpg123", "mpg123", "-R", "--rva-mix", "2> &1", NULL );
            fail( errno, "Could not exec mpg123" );
        }

        close( control.p_command[i][0] );
        close( control.p_status[i][1] );
    }

    /* first thing to be called after the GUI is enabled */
    gdk_threads_add_idle( initAll, &control );

    /* Add keyboard handler // @TODO */
/*    gtk_widget_add_events(control.widgets->mixplay_main, GDK_KEY_PRESS_MASK); */
/*    g_signal_connect (G_OBJECT (window), "keyboard_press", G_CALLBACK (on_key_press), NULL); */

    /* Start main loop */
    gtk_main();
    printver( 2, "Dropped out of the gtk_main loop\n" );
    control.status=mpc_quit;

    pthread_join( control.rtid, NULL );
    for( i=0; i <= control.fade; i++ ) {
    	kill( pid[i], SIGTERM );
    }

    /* Free any allocated data */
    free( control.dbname );
    free( control.dnpname );
    free( control.favname );
    free( control.musicdir );
    g_strfreev( control.profile );
    g_strfreev( control.stream );
    g_slice_free( MpData, control.widgets );
    control.root=cleanTitles( control.root );
    dbClose( db );

	return 0;
}
