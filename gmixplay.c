#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"
#include "player.h"
#include "gmixplay_app.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <X11/Xlib.h>

// global control structure
struct mpcontrol_t *mpcontrol;

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 */
static int initAll( void *data ) {
    struct mpcontrol_t *control;
    control=( struct mpcontrol_t* )data;
    readConfig( control );
    pthread_t tid;

    control->current=control->root;
    control->log[0]='\0';
    strcpy( control->playtime, "00:00" );
    strcpy( control->remtime, "00:00" );
    control->percent=0;
    control->status=mpc_idle;
    control->command=mpc_idle;
    pthread_create( &control->rtid, NULL, reader, control );

    if( NULL == control->root ) {
        // Runs as thread to have updates in the UI
        pthread_create( &tid, NULL, setProfile, ( void * )control );
    }
    else {
    	control->active=0;
        control->dbname[0]=0;
        setCommand(control, mpc_play );
    }

    if( control->debug ) {
        progressEnd( "Initialization done." );
    }
    return 0;
}

/**
 * sets up the UI by glade definitions
 */
static void buildUI( struct mpcontrol_t * control ) {
    GtkBuilder *builder;

    builder=gtk_builder_new_from_string( ( const char * )gmixplay_app_glade, gmixplay_app_glade_len );

    /* Allocate data structure */
    control->widgets = g_slice_new( MpData );

    /* Get objects from UI */
#define GW( name ) MP_GET_WIDGET( builder, name, control->widgets )
    GW( mixplay_main );
    GW( button_prev );
    GW( button_next );
    GW( title_current );
    GW( artist_current );
    GW( album_current );
    GW( genre_current );
    GW( button_play );
    GW( button_fav );
    GW( played );
    GW( remain );
    GW( progress );
    GW( button_profile );
#undef GW
    control->widgets->mp_popup = NULL;

    /* Connect signals */
    gtk_builder_connect_signals( builder, control->widgets );

    /* Destroy builder, since we don't need it anymore */
    g_object_unref( G_OBJECT( builder ) );

    /* Show window. All other widgets are automatically shown by GtkBuilder */
    gtk_widget_show( control->widgets->mixplay_main );

    if( control->fullscreen ) {
        gtk_window_fullscreen( GTK_WINDOW( control->widgets->mixplay_main ) );
    }
}

int main( int argc, char **argv ) {
    unsigned char	c;
    struct 		mpcontrol_t control;
    int			i;
    int 		db=0;
    pid_t		pid[2];

    mpcontrol=&control;

    memset( mpcontrol, 0, sizeof( struct mpcontrol_t ) );

    muteVerbosity();

    /* Init GTK+ */
    XInitThreads();
    gtk_init( &argc, &argv );

    control.fullscreen=0;
    control.debug=0;

    // parse command line options
    // using unsigned char c to work around getopt bug on ARM
    while ( ( c = getopt( argc, argv, "vfd" ) ) != 255 ) {
        switch ( c ) {
        case 'v': // increase debug message level to display in console output
            incVerbosity();
            break;

        case 'f':
            control.fullscreen=1;
            break;

        case 'd': // increase debug message level to display in debug request
            control.debug++;
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

    // start the player processes
    // these may wait in the background until
    // something needs to be played at all
    for( i=0; i <= 1; i++ ) {
        // create communication pipes
        pipe( control.p_status[i] );
        pipe( control.p_command[i] );
        pid[i] = fork();

        if ( 0 > pid[i] ) {
            fail( errno, "could not fork" );
        }

        // child process
        if ( 0 == pid[i] ) {
            printver( 2, "Starting player %i\n", i+1 );

            if ( dup2( control.p_command[i][0], STDIN_FILENO ) != STDIN_FILENO ) {
                fail( errno, "Could not dup stdin for player %i", i+1 );
            }

            if ( dup2( control.p_status[i][1], STDOUT_FILENO ) != STDOUT_FILENO ) {
                fail( errno, "Could not dup stdout for player %i", i+1 );
            }

            // this process needs no pipes
            close( control.p_command[i][0] );
            close( control.p_command[i][1] );
            close( control.p_status[i][0] );
            close( control.p_status[i][1] );
            // Start mpg123 in Remote mode
            execlp( "mpg123", "mpg123", "-R", "2> &1", NULL );
            fail( errno, "Could not exec mpg123" );
        }

        close( control.p_command[i][0] );
        close( control.p_status[i][1] );
    }

    // first thing to be called after the GUI is enabled
    gdk_threads_add_idle( initAll, &control );

    // Add keyboard handler // @TODO
//    gtk_widget_add_events(control.widgets->mixplay_main, GDK_KEY_PRESS_MASK);
//    g_signal_connect (G_OBJECT (window), "keyboard_press", G_CALLBACK (on_key_press), NULL);

    /* Start main loop */
    gtk_main();
    control.status=mpc_quit;

    pthread_join( control.rtid, NULL );
    kill( pid[0], SIGTERM );
    kill( pid[1], SIGTERM );

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

    return( 0 );
}
