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
#include "mpcomm.h"

/**
 * sets up the UI by glade definitions
 */
static void buildUI( mpconfig *control ) {
	GtkBuilder *builder;

	builder=gtk_builder_new_from_string( ( const char * )static_gmixplay_app_glade, static_gmixplay_app_glade_len );

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
	GW( volume );
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
	mpconfig *control;
	struct		glcontrol_t glcontrol;
	int 		db=0;

	/* read default configuration */
	control=readConfig();
	if( NULL == control ) {
		/* todo: get default music dir! */
		writeConfig( NULL );
		control=readConfig();
	}

	/* add special GTK data */
	memset( &glcontrol, 0, sizeof( struct glcontrol_t ) );
	glcontrol.msgbuff=msgBuffInit();
	control->data=&glcontrol;

	muteVerbosity();

	/* Init GTK+ */
	XInitThreads();
	gtk_init( &argc, &argv );

	/* parse command line */
	if( ( getArgs( argc, argv ) != 0 ) && ( control->remote == 1 ) ) {
		addMessage( 0, "Disabling remote connection" );
		control->remote=0;
	}

	buildUI( control );
	addProgressHook( &g_progressHook );
	addUpdateHook( &g_updateHook );
	initAll( 1 );

	/* Start main loop */
	control->inUI=-1;
	gtk_main();
	control->inUI=0;
	addMessage( 2, "Dropped out of the gtk_main loop" );
	control->status=mpc_quit;
	if( control->changed ) {
		writeConfig(NULL);
	}

	freeConfig( );
	dbClose( db );
	return( 0 );
}
