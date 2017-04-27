#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"
#include "player.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

struct mpcontrol_t *mpcontrol;


static void loadConfig( struct mpcontrol_t *config ) {
	GKeyFile	*keyfile;
	GError		*error=NULL;
	char		confdir[MAXPATHLEN]; // = "~/.mixplay";
	char		conffile[MAXPATHLEN]; //  = "mixplay.conf";
	GtkWidget 	*dialog;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
	gint 		res;

	// load default configuration
	snprintf( confdir, MAXPATHLEN, "%s/.mixplay", getenv("HOME") );
	snprintf( conffile, MAXPATHLEN, "%s/mixplay.conf", confdir );
	config->dbname=calloc( MAXPATHLEN, sizeof( char ) );
	snprintf( config->dbname, MAXPATHLEN, "%s/mixplay.db", confdir );

	if( !isDir( confdir ) ) {
		if( mkdir( confdir, 0700 ) == -1 ) {
			fail( errno, "Could not create config dir %s", confdir );
		}
	}

	keyfile=g_key_file_new();
	g_key_file_load_from_file( keyfile, conffile, G_KEY_FILE_NONE, &error );
	if( NULL != error ) {

		if( config->debug ) fail( F_WARN, "Could not load config from %s\n%s", conffile, error->message );
		error=NULL;
		dialog = gtk_file_chooser_dialog_new ("Select Music directory",
		                                      GTK_WINDOW( config->widgets->mixplay_main ),
		                                      action,
		                                      "_Cancel",
		                                      GTK_RESPONSE_CANCEL,
		                                      "_Okay",
		                                      GTK_RESPONSE_ACCEPT,
		                                      NULL);
		if( config->fullscreen ) {
			gtk_window_fullscreen( GTK_WINDOW( dialog) );
		}
		res = gtk_dialog_run( GTK_DIALOG ( dialog ) );
		if (res == GTK_RESPONSE_ACCEPT) {
		    config->musicdir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) );
		    g_key_file_set_string( keyfile, "mixplay", "musicdir", config->musicdir );
		    g_key_file_save_to_file( keyfile, conffile, &error );
		    if( NULL != error ) {
		    	fail( F_FAIL, "Could not write configuration!\n%s", error->message );
		    }
			gtk_widget_destroy (dialog);
		}
		else {
			fail( F_FAIL, "No Music directory chosen!" );
		}
	}

	config->musicdir=g_key_file_get_string( keyfile, "mixplay", "musicdir", &error );
	if( NULL != error ) {
		fail( F_FAIL, "No music dir set in configuration!\n%s", error->message );
	}
	config->profile =g_key_file_get_string_list( keyfile, "mixplay", "profiles", &config->profiles, &error );
	if( NULL != error ) {
		error=NULL;
		config->profile=calloc(1, sizeof( char * ) );
		config->profile[0]=calloc( 8, sizeof( char ) );
		strcpy( config->profile[0], "mixplay" );
		config->active=0;
	}
	else {
		config->active  =g_key_file_get_uint64( keyfile, "mixplay", "active", &error );
		if( NULL != error ) {
			fail( F_FAIL, "No active profile set!" );
		}
	}
	config->stream=g_key_file_get_string_list( keyfile, "mixplay", "streams", &config->streams, &error );
	if( NULL == error ) {
		config->sname =g_key_file_get_string_list( keyfile, "mixplay", "snames", &config->streams, &error );
		if( NULL != error ) {
			fail( F_FAIL, "Streams set but no names!\n%s", error->message );
		}
	}
}

/**
 * the control thread to communicate with the mpg123 processes
 */
/**
 * should be triggered after the app window is realized
 */
int initAll( void *data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t*)data;
	loadConfig( control );

	control->root=NULL;
	control->current=NULL;
	control->log[0]='\0';
	control->stream=0;
	strcpy( control->playtime, "00:00" );
	strcpy( control->remtime, "00:00" );
	control->percent=0;
	control->status=mpc_idle;
	pthread_create( &control->rtid, NULL, reader, control );
	setProfile( control );
	if( control->debug ) progressDone();
	return 0;
}

int main( int argc, char **argv ) {
    GtkBuilder *builder;
    GError     *error = NULL;
    char 		c;
    struct 		mpcontrol_t control;

    int			i;
    int 		db=0;
	pid_t		pid[2];

	mpcontrol=&control;

	char basedir[MAXPATHLEN]; // = ".";
	// if no basedir has been set, use the current directory as default
	if( 0 == strlen(basedir) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
		fail( errno, "Could not get current directory!" );
	}

	muteVerbosity();

	memset( basedir, 0, MAXPATHLEN );

    /* Init GTK+ */
    gtk_init( &argc, &argv );

    control.fullscreen=0;
    control.debug=0;

	// parse command line options
	while ((c = getopt(argc, argv, "vfd")) != -1) {
		switch (c) {
		case 'v': // pretty useless in normal use
			control.debug=1;
			incVerbosity();
		break;
		case 'f':
			control.fullscreen=1;
		break;
		case 'd':
			control.debug=1;
		break;
		}
	}

    /* Create new GtkBuilder object */
    builder = gtk_builder_new();

    if( control.fullscreen ) {
    	gtk_builder_add_from_file( builder, "gmixplay_fs.glade", &error );
    }
    else {
    	gtk_builder_add_from_file( builder, "gmixplay_app.glade", &error );
    }
    if( NULL != error  )
    {
        g_warning( "%s", error->message );
        g_free( error );
        return( 1 );
    }

    /* Allocate data structure */
    control.widgets = g_slice_new( MpData );

    /* Get objects from UI */
#define GW( name ) MP_GET_WIDGET( builder, name, control.widgets )
	GW( mixplay_main );
	GW( button_prev );
	GW( button_next );
	GW( button_replay );
	GW( image_current );
	GW( title_current );
	GW( artist_current );
	GW( album_current );
	GW( genre_current );
	GW( displayname_prev );
	GW( displayname_next );
	GW( button_dnp );
	GW( button_play );
	GW( button_fav );
	GW( played );
	GW( remain );
	GW( progress );
	GW( pause );
	GW( play );
	GW( down );
	GW( skip );
	GW( noentry );
#undef GW
	control.widgets->mp_popup = NULL;

	g_object_ref(control.widgets->play );
	g_object_ref(control.widgets->pause );
	g_object_ref(control.widgets->down );
	g_object_ref(control.widgets->noentry );

    /* Connect signals */
    gtk_builder_connect_signals( builder, control.widgets );

    /* Destroy builder, since we don't need it anymore */
    g_object_unref( G_OBJECT( builder ) );

	/* Show window. All other widgets are automatically shown by GtkBuilder */
    gtk_widget_show( control.widgets->mixplay_main );
    if( control.fullscreen ) {
    	gtk_window_fullscreen( GTK_WINDOW( control.widgets->mixplay_main) );
    }

    if( control.debug ) {
    	progressLog("Debug");
    }

	// start the player processes
	// these may wait in the background until
	// something needs to be played at all
	for( i=0; i <= 1; i++ ) {
		// create communication pipes
		pipe(control.p_status[i]);
		pipe(control.p_command[i]);
		pid[i] = fork();
		if (0 > pid[i]) {
			fail( errno, "could not fork" );
		}
		// child process
		if (0 == pid[i]) {
			if (dup2(control.p_command[i][0], STDIN_FILENO) != STDIN_FILENO) {
				fail( errno, "Could not dup stdin for player %i", i+1 );
			}
			if (dup2(control.p_status[i][1], STDOUT_FILENO) != STDOUT_FILENO) {
				fail( errno, "Could not dup stdout for player %i", i+1 );
			}
			// this process needs no pipes
			close(control.p_command[i][0]);
			close(control.p_command[i][1]);
			close(control.p_status[i][0]);
			close(control.p_status[i][1]);
			// Start mpg123 in Remote mode
			execlp("mpg123", "mpg123", "-R", "2>/dev/null", NULL);
			// execlp("mpg123", "mpg123", "-R", "--remote-err", NULL); // breaks the reply parsing!
			fail( errno, "Could not exec mpg123" );
		}
		close(control.p_command[i][0]);
		close(control.p_status[i][1]);
	}

    // first thing to be called after the GUI is enabled
    g_idle_add( initAll, &control );

    /* Start main loop */
    gtk_main();
    control.status=mpc_quit;

    pthread_join( control.rtid, NULL );
	kill( pid[0], SIGTERM);
	kill( pid[1], SIGTERM );

    /* Free any allocated data */
    g_slice_free( MpData, control.widgets );
    cleanTitles( control.root );
	dbClose( db );

	return( 0 );
}
