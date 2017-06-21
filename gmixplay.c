#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"
#include "player.h"
#include "gmixplay_app.h"
// #include "gmixplay_fs.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <X11/Xlib.h>

struct mpcontrol_t *mpcontrol;

/**
 * load configuration file
 * if the file does not exist, create new configuration
 */
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
	config->dbname=falloc( MAXPATHLEN, sizeof( char ) );
	snprintf( config->dbname, MAXPATHLEN, "%s/mixplay.db", confdir );

	if( !isDir( confdir ) ) {
		if( mkdir( confdir, 0700 ) == -1 ) {
			fail( errno, "Could not create config dir %s", confdir );
		}
	}

	keyfile=g_key_file_new();
	g_key_file_load_from_file( keyfile, conffile, G_KEY_FILE_KEEP_COMMENTS, &error );
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
		config->profile=falloc(1, sizeof( char * ) );
		config->profile[0]=falloc( 8, sizeof( char ) );
		strcpy( config->profile[0], "mixplay" );
		config->active=0;
		g_key_file_set_string_list( keyfile, "mixplay", "profiles", (const char* const*)config->profile, 1 );
		g_key_file_set_uint64( keyfile, "mixplay", "active", 0 );
	    g_key_file_save_to_file( keyfile, conffile, &error );
	    if( NULL != error ) {
	    	fail( F_FAIL, "Could not write configuration!\n%s", error->message );
	    }
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
	g_key_file_free( keyfile );
}

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 */
static int initAll( void *data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t*)data;
	loadConfig( control );
	pthread_t tid;


	control->current=NULL;
	control->log[0]='\0';
	control->stream=0;
	strcpy( control->playtime, "00:00" );
	strcpy( control->remtime, "00:00" );
	control->percent=0;
	control->status=mpc_idle;
	control->command=mpc_idle;
	pthread_create( &control->rtid, NULL, reader, control );
	if( NULL == control->root ) {
		pthread_create( &tid, NULL, setProfile, (void *)control );
//		setProfile( control );
	}
	else {
		control->dbname[0]=0;
		setCommand( control, mpc_start );
		if( control->debug ) progressDone( "Initialization done.");
	}
	return 0;
}

/**
 * sets up the UI by glade definitions
 */
static void buildUI( struct mpcontrol_t * control ) {
    GtkBuilder *builder;

   	builder=gtk_builder_new_from_string( (const char *)gmixplay_app_glade, gmixplay_app_glade_len );

    /* Allocate data structure */
    control->widgets = g_slice_new( MpData );

    /* Get objects from UI */
#define GW( name ) MP_GET_WIDGET( builder, name, control->widgets )
	GW( mixplay_main );
	GW( button_prev );
	GW( button_next );
	GW( button_replay );
	GW( title_current );
	GW( artist_current );
	GW( album_current );
	GW( genre_current );
	GW( button_dnp );
	GW( button_play );
	GW( button_fav );
	GW( button_database );
	GW( played );
	GW( remain );
	GW( progress );
#undef GW
	control->widgets->mp_popup = NULL;

    /* Connect signals */
    gtk_builder_connect_signals( builder, control->widgets );

    /* Destroy builder, since we don't need it anymore */
    g_object_unref( G_OBJECT( builder ) );

	/* Show window. All other widgets are automatically shown by GtkBuilder */
    gtk_widget_show( control->widgets->mixplay_main );

    if( control->fullscreen ) {
    	gtk_window_fullscreen( GTK_WINDOW( control->widgets->mixplay_main) );
    }
}

int main( int argc, char **argv ) {
    unsigned char	c;
    struct 		mpcontrol_t control;
    char		line [MAXPATHLEN];

    int			i;
    int 		db=0;
	pid_t		pid[2];

	mpcontrol=&control;
	muteVerbosity();

    /* Init GTK+ */
	XInitThreads();
    gtk_init( &argc, &argv );

    control.fullscreen=0;
    control.debug=0;

	// parse command line options
    // using unsigned char *c to work around getopt bug on ARM
	while ((c = getopt(argc, argv, "vfd")) != 255 ) {
		switch (c) {
		case 'v': // pretty useless in normal use
			incVerbosity();
		break;
		case 'f':
			control.fullscreen=1;
		break;
		case 'd':
			control.debug++;
		break;
		}
	}

	buildUI( &control );

	control.root=NULL;
	control.playstream=0;

	if (optind < argc) {
		if( isURL( argv[optind] ) ) {
			control.playstream=1;
			line[0]=0;
			if( endsWith( argv[optind], ".m3u" ) ||
					endsWith( argv[optind], ".pls" ) ) {
				fail( F_FAIL, "Only direct stream support" );
				strcpy( line, "@" );
			}
			strncat( line, argv[optind], MAXPATHLEN );
			printver(1, "Stream address: %s\n", line );
			control.root=insertTitle( NULL, line );
			strncpy( control.root->title, "Waiting for stream info...", NAMELEN );
			insertTitle( control.root, control.root->title );
			insertTitle( control.root, control.root->title );
			addToPL( control.root->dbnext, control.root );
		}
		else if( endsWith( argv[optind], ".mp3" ) ) {
			// play single song...
			control.root=insertTitle( NULL, argv[optind] );
		}
		else if( isDir( argv[optind]) ) { // @todo: playlist linking
			control.root=recurse(argv[optind], NULL, argv[optind]);
			control.root=control.root->dbnext;
		}
		else if ( endsWith( argv[optind], ".m3u" ) ||
				endsWith( argv[optind], ".pls" ) ) {
			if( NULL != strrchr( argv[optind], '/' ) ) {
				strcpy(line, argv[optind]);
				i=strlen(line);
				while( line[i] != '/' ) i--;
				line[i]=0;
				chdir(line);
			}
			control.root=loadPlaylist( argv[optind] );
		}
		else {
			fail(F_FAIL, "Unknown argument!\n", argv[optind] );
			return -1;
		}
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
			printver( 2, "Starting player %i\n", i+1 );
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
			// execlp("mpg123", "mpg123", "-R", "2>/dev/null", NULL);
			// execlp("mpg123", "mpg123", "-R", "--remote-err", NULL); // breaks the reply parsing!
			execlp("mpg123", "mpg123", "-R", "2> &1", NULL);
			fail( errno, "Could not exec mpg123" );
		}
		close(control.p_command[i][0]);
		close(control.p_status[i][1]);
	}

    // first thing to be called after the GUI is enabled
    gdk_threads_add_idle( initAll, &control );

    /* Start main loop */
    gtk_main();
    control.status=mpc_quit;

    pthread_join( control.rtid, NULL );
	kill( pid[0], SIGTERM);
	kill( pid[1], SIGTERM );

    /* Free any allocated data */
	free( control.dbname );
	free( control.dnpname );
	free( control.favname );
	free( control.musicdir );
	g_strfreev( control.profile );
	g_strfreev( control.stream );
    g_slice_free( MpData, control.widgets );
    cleanTitles( control.root );
	dbClose( db );

	return( 0 );
}
