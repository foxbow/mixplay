#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <X11/Xlib.h>

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"
#include "gmixplay_app.h"
#include "player.h"
#include "config.h"

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 */
static int initAll( void *data ) {
    struct mpcontrol_t *control;

    control=readConfig();

    pthread_t tid;

    MP_GLDATA->log[0]='\0';

    pthread_create( &control->rtid, NULL, reader, control );

    if( NULL == control->root ) {
        /* Runs as thread to have updates in the UI */
        pthread_create( &tid, NULL, setProfile, ( void * )control );
    }
    else {
    	control->active=0;
        control->dbname[0]=0;
        setCommand( mpc_play );
    }

    if( MP_GLDATA->debug ) {
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
    MP_GLDATA->widgets = g_slice_new( MpData );

    /* Get objects from UI */
#define GW( name ) MP_GET_WIDGET( builder, name, MP_GLDATA->widgets )
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
    MP_GLDATA->widgets->mp_popup = NULL;

    /* Connect signals */
    gtk_builder_connect_signals( builder, MP_GLDATA->widgets );

    /* Destroy builder, since we don't need it anymore */
    g_object_unref( G_OBJECT( builder ) );

    /* Show window. All other widgets are automatically shown by GtkBuilder */
    gtk_widget_show( MP_GLDATA->widgets->mixplay_main );

    if( MP_GLDATA->fullscreen ) {
        gtk_window_fullscreen( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ) );
    }
}

int main( int argc, char **argv ) {
    unsigned char	c;
    struct 		mpcontrol_t *control;
    struct		glcontrol_t glcontrol;
    int 		db=0;

    control=readConfig();
    if( NULL == control ) {
    	control=writeConfig( NULL );
    }

    memset( &glcontrol, 0, sizeof( struct glcontrol_t ) );

    control->data=&glcontrol;

    muteVerbosity();

    /* Init GTK+ */
    XInitThreads();
    gtk_init( &argc, &argv );

    control->fade=1;
    glcontrol.fullscreen=0;
    glcontrol.debug=0;

    /* parse command line options */
    /* using unsigned char c to work around getopt bug on ARM */
    while ( ( c = getopt( argc, argv, "vfdF" ) ) != 255 ) {
        switch ( c ) {
        case 'v': /* increase debug message level to display in console output */
            incVerbosity();
            break;

        case 'f':
            glcontrol.fullscreen=1;
            break;

        case 'd': /* increase debug message level to display in debug request */
            glcontrol.debug++;
            break;

        case 'F': /* single channel - disable fading */
        	control->fade=0;
        	break;
        }
    }

    buildUI( control );

    control->root=NULL;
    control->playstream=0;

    if ( optind < argc ) {
    	if( 0 == setArgument( argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    initAll( &control );

    /* Start main loop */
    control->inUI=-1;
    gtk_main();
    control->inUI=0;
    addMessage( 2, "Dropped out of the gtk_main loop" );
    control->status=mpc_quit;

    pthread_join( control->rtid, NULL );

    freeConfig( );

    dbClose( db );

    return( 0 );
}
