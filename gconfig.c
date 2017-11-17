/*
 * gconfig.c
 *
 * GTK implementation of config.h
 *
 *  Created on: 16.11.2017
 *      Author: bweber
 *
 * @deprecated - to be replaced with config.c
 */
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#include "gladeutils.h"
#include "musicmgr.h"
#include "config.h"

/**
 * writes the current configuration
 */
void writeConfig( struct mpcontrol_t *config ) {
    char		conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    GKeyFile	*keyfile;
    GError		*error=NULL;

    printver( 1, "Writing config\n" );

    snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );
    keyfile=g_key_file_new();

    g_key_file_set_string( keyfile, "mixplay", "musicdir", config->musicdir );
    g_key_file_set_string_list( keyfile, "mixplay", "profiles", ( const char* const* )config->profile, config->profiles );
    if( config->streams > 0 ) {
        g_key_file_set_string_list( keyfile, "mixplay", "streams", ( const char* const* )config->stream, config->streams );
        g_key_file_set_string_list( keyfile, "mixplay", "snames", ( const char* const* )config->sname, config->streams );
    }
    g_key_file_set_int64( keyfile, "mixplay", "active", config->active );
    g_key_file_set_int64( keyfile, "mixplay", "skipdnp", config->skipdnp );
    g_key_file_set_int64( keyfile, "mixplay", "fade", config->fade );
    g_key_file_save_to_file( keyfile, conffile, &error );

    if( NULL != error ) {
        fail( F_FAIL, "Could not write configuration!\n%s", error->message );
    }
}

/**
 * load configuration file
 * if the file does not exist, create new configuration
 */
void readConfig( struct mpcontrol_t *config ) {
    GKeyFile	*keyfile;
    GError		*error=NULL;
    char		confdir[MAXPATHLEN];  /* = "$HOME/.mixplay"; */
    char		conffile[MAXPATHLEN]; /* = "mixplay.conf"; */
    GtkWidget 	*dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    gint 		res;
    gsize		snum;

    printver( 1, "Reading config\n" );

    /* load default configuration */
    snprintf( confdir, MAXPATHLEN, "%s/.mixplay", getenv( "HOME" ) );
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
        if( gldata(config)->debug ) {
            fail( F_WARN, "Could not load config from %s\n%s", conffile, error->message );
        }

        error=NULL;
        dialog = gtk_file_chooser_dialog_new ( "Select Music directory",
                                               GTK_WINDOW( gldata(config)->widgets->mixplay_main ),
                                               action,
                                               "_Cancel",
                                               GTK_RESPONSE_CANCEL,
                                               "_Okay",
                                               GTK_RESPONSE_ACCEPT,
                                               NULL );

        if( gldata(config)->fullscreen ) {
            gtk_window_fullscreen( GTK_WINDOW( dialog ) );
        }

        res = gtk_dialog_run( GTK_DIALOG ( dialog ) );

        if ( res == GTK_RESPONSE_ACCEPT ) {
        	/* Set minimum defaults to let mixplay work */
        	config->musicdir=falloc( MAXPATHLEN, sizeof( char ) );
            strncpy( config->musicdir, gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) ), MAXPATHLEN );
            gtk_widget_destroy ( dialog );
            config->profile=falloc( 1, sizeof( char * ) );
            config->profile[0]=falloc( 8, sizeof( char ) );
            strcpy( config->profile[0], "mixplay" );
            config->profiles=1;
            config->active=1;
            config->skipdnp=3;
            writeConfig( config );
            return;
        }
        else {
            fail( F_FAIL, "No Music directory chosen!" );
        }
    }

    /* get music dir */
    config->musicdir=g_key_file_get_string( keyfile, "mixplay", "musicdir", &error );
    if( NULL != error ) {
        fail( F_FAIL, "No music dir set in configuration!\n%s", error->message );
    }

    /* read list of profiles */
    config->profile =g_key_file_get_string_list( keyfile, "mixplay", "profiles", &config->profiles, &error );
    if( NULL != error ) {
    	fail( F_FAIL, "No profiles set in config file!\n%s", error->message );
    }

    /* get active configuration */
	config->active  =g_key_file_get_uint64( keyfile, "mixplay", "active", &error );
	if( NULL != error ) {
		fail( F_FAIL, "No active profile set in config!\n%s", error->message );
	}

	config->skipdnp  =g_key_file_get_uint64( keyfile, "mixplay", "skipdnp", &error );

	/* Ignore fade setting if if has been overridden */
	if( config->fade == 1 ) {
		config->fade  =g_key_file_get_uint64( keyfile, "mixplay", "fade", &error );
		if( NULL != error ) {
			config->fade=1;
		}
	}

	/* read streams if any are there */
    config->stream=g_key_file_get_string_list( keyfile, "mixplay", "streams", &config->streams, &error );
    if( NULL == error ) {
    	snum=config->streams;
        config->sname =g_key_file_get_string_list( keyfile, "mixplay", "snames", &config->streams, &error );
        if( NULL != error ) {
            fail( F_FAIL, "Streams set but no names!\n%s", error->message );
        }
        if( snum != config->streams ) {
        	fail( F_FAIL, "Read %i streams but %i names!", snum, config->streams );
        }
    }

    g_key_file_free( keyfile );
}

