/*
 * player.c
 *
 *  Created on: 26.04.2017
 *      Author: bweber
 */
#include "player.h"

#include <assert.h>
#include "database.h"
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>

pthread_mutex_t cmdlock=PTHREAD_MUTEX_INITIALIZER;

/**
 * writes the current configuration
 */
void writeConfig( struct mpcontrol_t *config ) {
    char		conffile[MAXPATHLEN]; //  = "mixplay.conf";
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
    char		confdir[MAXPATHLEN];  // = "$HOME/.mixplay";
    char		conffile[MAXPATHLEN]; // = "mixplay.conf";
    GtkWidget 	*dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    gint 		res;
    gsize		snum;

    printver( 1, "Reading config\n" );

    // load default configuration
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
        if( config->debug ) {
            fail( F_WARN, "Could not load config from %s\n%s", conffile, error->message );
        }

        error=NULL;
        dialog = gtk_file_chooser_dialog_new ( "Select Music directory",
                                               GTK_WINDOW( config->widgets->mixplay_main ),
                                               action,
                                               "_Cancel",
                                               GTK_RESPONSE_CANCEL,
                                               "_Okay",
                                               GTK_RESPONSE_ACCEPT,
                                               NULL );

        if( config->fullscreen ) {
            gtk_window_fullscreen( GTK_WINDOW( dialog ) );
        }

        res = gtk_dialog_run( GTK_DIALOG ( dialog ) );

        if ( res == GTK_RESPONSE_ACCEPT ) {
        	// Set minimum defaults to let mixplay work
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

    // get music dir
    config->musicdir=g_key_file_get_string( keyfile, "mixplay", "musicdir", &error );
    if( NULL != error ) {
        fail( F_FAIL, "No music dir set in configuration!\n%s", error->message );
    }

    // read list of profiles
    config->profile =g_key_file_get_string_list( keyfile, "mixplay", "profiles", &config->profiles, &error );
    if( NULL != error ) {
    	fail( F_FAIL, "No profiles set in config file!\n%s", error->message );
    }

    // get active configuration
	config->active  =g_key_file_get_uint64( keyfile, "mixplay", "active", &error );
	if( NULL != error ) {
		fail( F_FAIL, "No active profile set in config!\n%s", error->message );
	}

	config->skipdnp  =g_key_file_get_uint64( keyfile, "mixplay", "skipdnp", &error );

	// Ignore fade setting if if has been overridden
	if( config->fade == 1 ) {
		config->fade  =g_key_file_get_uint64( keyfile, "mixplay", "fade", &error );
		if( NULL != error ) {
			config->fade=1;
		}
	}

	// read streams if any are there
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

/**
 * implements command queue
 */
void setCommand( struct mpcontrol_t *control, mpcmd cmd ) {
    pthread_mutex_lock( &cmdlock );
    control->command=cmd;
}

/**
 * parse arguments given to the application
 * also handles playing of a single file, a directory, a playlist or an URL
 * returns 0 if nothing was recognized
 */
int setArgument( struct mpcontrol_t *control, const char *arg ) {
    char line [MAXPATHLEN];
    int  i;

	control->active=0;
    if( isURL( arg ) ) {
        control->playstream=1;
        line[0]=0;

        if( endsWith( arg, ".m3u" ) ||
                endsWith( arg, ".pls" ) ) {
            fail( F_FAIL, "Only direct stream support" );
            strcpy( line, "@" );
        }

        strncat( line, arg, MAXPATHLEN );
        control->root=cleanTitles( control->root );
        setStream( control, line, "Waiting for stream info..." );
        return 1;
    }
    else if( endsWith( arg, ".mp3" ) ) {
        // play single song...
    	control->root=cleanTitles( control->root );
        control->root=insertTitle( NULL, arg );
        return 2;
    }
    else if( isDir( arg ) ) {
    	control->root=cleanTitles( control->root );
        strncpy( line, arg, MAXPATHLEN );
        control->root=recurse( line, NULL, arg );
        if( control->root != NULL ) {
			control->current=control->root;
			do {
				control->current->plnext=control->current->dbnext;
				control->current->plprev=control->current->dbprev;
				control->current=control->current->dbnext;
			} while( control->current != control->root );
        }
        return 3;
    }
    else if ( endsWith( arg, ".m3u" ) ||
              endsWith( arg, ".pls" ) ) {
        if( NULL != strrchr( arg, '/' ) ) {
            strcpy( line, arg );
            i=strlen( line );

            while( line[i] != '/' ) {
                i--;
            }

            line[i]=0;
            chdir( line );
        }

        cleanTitles( control->root );
        control->root=loadPlaylist( arg );
        return 4;
    }

    return 0;
}

/**
 * make mpeg123 play the given title
 */
static void sendplay( int fdset, struct mpcontrol_t *control ) {
    char line[MAXPATHLEN]="load ";
    strncat( line, control->current->path, MAXPATHLEN );
    strncat( line, "\n", MAXPATHLEN );

    if ( write( control->p_command[fdset][1], line, MAXPATHLEN ) < strlen( line ) ) {
        fail( F_FAIL, "Could not write\n%s", line );
    }

    printver( 2, "CMD: %s", line );
}

/**
 * sets the given stream
 */
void setStream( struct mpcontrol_t *control, const char* stream, const char *name ) {
	control->root=cleanTitles( control->root );
    control->root=insertTitle( NULL, stream );
    strncpy( control->root->title, name, NAMELEN );
    insertTitle( control->root, control->root->title );
    addToPL( control->root->dbnext, control->root );
    strncpy( control->root->dbnext->display, "---", 3 );
    printver( 1, "Play Stream %s\n-> %s\n", name, stream );
}

/**
 * sets the current profile
 * This is thread-able to have progress information on startup!
 */
void *setProfile( void *data ) {
    char		confdir[MAXPATHLEN]; // = "~/.mixplay";
    char 		*profile;
    struct marklist_t *dnplist=NULL;
    struct marklist_t *favourites=NULL;
    int num;
    int lastver;
    int64_t active;
    struct mpcontrol_t *control=( struct mpcontrol_t * )data ;

    active=control->active;

    if( active < 0 ) {
        control->playstream=1;
    	active = -(active+1);

        if( active >= control->streams ) {
            fail( F_FAIL, "Stream #%i does no exist!", active );
        }

        setStream( control, control->stream[active], control->sname[active] );
    }
    else if( active > 0 ){
    	active=active-1;

        if( active > control->profiles ) {
            fail( F_FAIL, "Profile #%i does no exist!", active );
        }

        if( NULL == control->profile[active] ) {
            fail( F_FAIL, "Profile #%i is not set!", active );
        }

        profile=control->profile[active];
        snprintf( confdir, MAXPATHLEN, "%s/.mixplay", getenv( "HOME" ) );

        control->dnpname=falloc( MAXPATHLEN, sizeof( char ) );
        snprintf( control->dnpname, MAXPATHLEN, "%s/%s.dnp", confdir, profile );

        control->favname=falloc( MAXPATHLEN, sizeof( char ) );
        snprintf( control->favname, MAXPATHLEN, "%s/%s.fav", confdir, profile );
        dnplist=loadList( control->dnpname );
        favourites=loadList( control->favname );

        control->root=cleanTitles( control->root );

        control->root=dbGetMusic( control->dbname );

        if( NULL == control->root ) {
            printver( 1, "Scanning musicdir" );
            lastver=getVerbosity();

            if( lastver < 1 ) {
                setVerbosity( 1 );
            }

            num = dbAddTitles( control->dbname, control->musicdir );

            if( 0 == num ) {
                fail( F_FAIL, "No music found at %s!", control->musicdir );
            }

            printver( 1, "Added %i titles.", num );
//            progressEnd( NULL ); // @TODO - why is this here?
            control->root=dbGetMusic( control->dbname );

            if( NULL == control->root ) {
                fail( F_FAIL, "No music found at %s for database %s!\nThis should never happen!",
                      control->musicdir,  control->dbname );
            }

            setVerbosity( lastver );
        }

        if( control->skipdnp ) {
        	DNPSkip( control->root, control->skipdnp );
        }
        applyDNPlist( control->root, dnplist );
        applyFavourites( control->root, favourites );
        control->root=shuffleTitles( control->root );
        cleanList( dnplist );
        cleanList( favourites );
        control->playstream=0;

        printver( 1, "Profile set to %s.\n", profile );
    }
    else {
    	fail( F_FAIL, "Neither profile nor stream set!" );
    }

    // if we're not in player context, start playing automatically
    if( pthread_mutex_trylock( &cmdlock ) == 0 ){
    	printver( 2, "Autoplay\n" );
        control->command=mpc_start;
    }

    return NULL;
}

/**
 * checks if the playcount needs to be increased and if the skipcount
 * needs to be decreased. In both cases the updated information is written
 * back into the db.
 */
static void playCount( struct mpcontrol_t *control ) {
    int db;

    if( control->playstream ) {
    	return;
    }

    if( ( control->current->key != 0 ) && !( control->current->flags & MP_CNTD ) ) {
        control->current->flags |= MP_CNTD; // make sure a title is only counted once per session
        control->current->playcount++;

        if( !( control->current->flags & MP_SKPD ) && ( control->current->skipcount > 0 ) ) {
            control->current->skipcount--;
        }

        db=dbOpen( control->dbname );
        dbPutTitle( db, control->current );
        dbClose( db );
    }
}

/**
 * the main control thread function that acts as an interface between the
 * player processes and the UI. It checks the control->command value for
 * commands from the UI and the status messages from the players.
 *
 * The original plan was to keep this UI independent so it could be used
 * in mixplay, gmixplay and probably other GUI variants (ie: web)
 */
void *reader( void *cont ) {
    struct mpcontrol_t *control;
    struct entry_t *next;
    fd_set fds;
    struct timeval to;
    int64_t i, key;
    int invol=100;
    int outvol=100;
    int redraw=0;
    int fdset=0;
    char line[MAXPATHLEN];
    char *a, *t;
    int order=1;
    int intime=0;
    int fade=3;

    // Debug stuff
    char *mpc_command[] = {
    	    "mpc_idle",
    	    "mpc_play",
    	    "mpc_stop",
    	    "mpc_prev",
    	    "mpc_next",
    	    "mpc_start",
    	    "mpc_favtitle",
    	    "mpc_favartist",
    	    "mpc_favalbum",
    	    "mpc_repl",
    	    "mpc_profile",
    	    "mpc_quit",
    	    "mpc_dbclean",
    	    "mpc_dnptitle",
    	    "mpc_dnpartist",
    	    "mpc_dnpalbum",
    	    "mpc_dnpgenre",
    		"mpc_doublets",
    		"mpc_shuffle",
    };

    control=( struct mpcontrol_t * )cont;
    assert( control->fade < 2 );

    if( control->fade == 0 ) {
    	fade=0;
    }

    printver( 2, "Reader started\n" );

    do {
        FD_ZERO( &fds );
        for( i=0; i<=control->fade; i++ ) {
        	FD_SET( control->p_status[i][0], &fds );
        }
        to.tv_sec=0;
        to.tv_usec=100000; // 1/10 second
        i=select( FD_SETSIZE, &fds, NULL, NULL, &to );

        if( i>0 ) {
            redraw=1;
        }

        // drain inactive player
        if( control->fade && FD_ISSET( control->p_status[fdset?0:1][0], &fds ) ) {
            key=readline( line, 512, control->p_status[fdset?0:1][0] );

            if( key > 2 ) {
				if( '@' == line[0] ) {
	        		if( ( 'F' != line[1] ) && ( 'V' != line[1] ) ) {
	        			printver( 2, "P- %s\n", line );
	        		}

					switch ( line[1] ) {
					case 'R': // startup
						printver( 1, "MPG123 background instance is up\n" );
						break;
					case 'E':
						fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
							  control->current->key,
							  control->current->display,
							  control->current->path );
						break;
					case 'F':
						if( ( key > 1 ) && ( outvol > 0 ) ) {
							outvol--;
							snprintf( line, MAXPATHLEN, "volume %i\n", outvol );
							write( control->p_command[fdset?0:1][1], line, strlen( line ) );
						}
						break;
					}
				}
				else {
					printver( 1, "OFF123: %s\n", line );
				}
            }
        }

        // Interpret mpg123 output and ignore invalid lines
        if( FD_ISSET( control->p_status[fdset][0], &fds ) &&
                ( 3 < readline( line, 512, control->p_status[fdset][0] ) ) ) {
        	if( '@' == line[0] ) {
        		// Don't print volume and progress messages
        		if( ( 'F' != line[1] ) && ( 'V' != line[1] ) ) {
        			printver( 2, "P+ %s\n", line );
        		}
                switch ( line[1] ) {
                    int cmd=0, rem=0;

                case 'R': // startup
                    printver( 1, "MPG123 foreground instance is up\n" );
                    break;

                case 'I': // ID3 info

                    // ICY stream info
                    if( NULL != strstr( line, "ICY-" ) ) {
                        if( NULL != strstr( line, "ICY-NAME: " ) ) {
                            strip( control->current->album, line+13, NAMELEN );
                        }

                        if( NULL != ( a = strstr( line, "StreamTitle" ) ) ) {
                            if( control->current->plnext == control->current ) {
                                fail( F_FAIL, "Messed up playlist!" );
                            }

                            printver( 3, "%s\n", a );
                            a = a + 13;
                            *strchr( a, '\'' ) = '\0';
                            strncpy( control->current->plnext->display, control->current->display, MAXPATHLEN );
                            strip( control->current->display, a, MAXPATHLEN );

                            if( NULL != ( t = strstr( a, " - " ) ) ) {
                                *t=0;
                                t=t+3;
                                strncpy( control->current->artist, a, NAMELEN );
                                strncpy( control->current->title, t, NAMELEN );
                            }
                            else {
                                strip( control->current->title, a, NAMELEN );
                            }
                        }
                    }
                    // standard mpg123 info
                    else if ( strstr( line, "ID3" ) != NULL ) {
                        // ignored
                    }

                    break;

                case 'T': // TAG reply
                    fail( F_FAIL, "Got TAG reply!" );
                    break;

                case 'J': // JUMP reply
                    redraw=0;
                    break;

                case 'S': // Status message after loading a song (stream info)
                    redraw=0;
                    break;

                case 'F': // Status message during playing (frame info)
                    /* $1   = framecount (int)
                     * $2   = frames left this song (int)
                     * in  = seconds (float)
                     * rem = seconds left (float)
                     */
                	a=strrchr( line, ' ' );
                    rem=atoi( a );
                    *a=0;
                    a=strrchr( line, ' ' );
                    intime=atoi( a );

                    if( intime/60 < 60 ) {
						sprintf( control->playtime, "%02i:%02i", intime/60, intime%60 );
					}
					else {
						sprintf( control->playtime, "%02i:%02i:%02i", intime/3600, ( intime%3600 )/60, intime%60 );
					}
                    // file play
                    if( control->playstream == 0 ) {
                        control->percent=( 100*intime )/( rem+intime );
                        sprintf( control->remtime, "%02i:%02i", rem/60, rem%60 );

                        if( rem <= fade ) {
                            // should the playcount be increased?
                            playCount( control );
                            next=control->current->plnext;

                            if( next == control->current ) {
                            	control->status=mpc_idle; // STOP
                            }
                            else {
                                control->current=next;
                                // swap player if we want to fade
                                if( control->fade ) {
                                	fdset=fdset?0:1;
                                	invol=0;
                                	outvol=100;
                                	write( control->p_command[fdset][1], "volume 0\n", 9 );
                                }
                                sendplay( fdset, control );
                            }
                        }

                        if( invol < 100 ) {
                            invol++;
                            snprintf( line, MAXPATHLEN, "volume %i\n", invol );
                            write( control->p_command[fdset][1], line, strlen( line ) );
                        }

                    }

                    redraw=1;
                    break;

                case 'P': // Player status
                    cmd = atoi( &line[3] );

                    switch ( cmd ) {
                    case 0: // STOP
                    	// player was not yet fully initialized, start again
                    	if( control->status != mpc_play ) {
                			sendplay( fdset, control );
                    	}
                        // should the playcount be increased?
                    	else if( control->playstream == 0 ) {
							playCount( control );
							next = skipTitles( control->current, order, 0 );

							if ( ( next == control->current ) ||
									( ( ( order == 1  ) && ( next == control->root ) ) ||
									  ( ( order == -1 ) && ( next == control->root->plprev ) ) ) ) {
								control->status=mpc_idle; // stop
							}
							else {
								if( ( order==1 ) && ( next == control->root ) ) {
									control->root=shuffleTitles( control->root );
									next=control->root;
								}

								control->current=next;
								sendplay( fdset, control );
							}

							order=1;
                    	}
                    	else {
                    		control->status=mpc_idle;
                    	}
                        break;

                    case 1: // PAUSE
                    	control->status=mpc_idle;
                        break;

                    case 2:  // PLAY
                    	if( control->status == mpc_start ) {
                        	printver( 2, "Playing profile #%i\n", control->active );
                    	}
                    	control->status=mpc_play;
                        break;

                    default:
                        fail( F_WARN, "Unknown status %i!\n%s", cmd, line );
                        break;
                    }

                    redraw=1;
                    break;

                case 'V': // volume reply
                    redraw=0;
                    break;

                case 'E':
                    fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
                          control->current->key,
                          control->current->display,
                          control->current->path );
                    break;

                default:
                    fail( F_WARN, "Warning!\n%s", line );
                    break;
                } // case line[1]
            } // if line starts with '@'
            else {
            	printver(1, "MPG123: %s\n", line );
            	// Ignore other mpg123 output
            }
        } // fgets() > 0

        if( redraw ) {
            updateUI( control );
        }

        pthread_mutex_trylock( &cmdlock );

        if( control->command != mpc_idle ) {
        	printver( 2, "MPC %s\n", mpc_command[control->command] );
        }
        switch( control->command ) {
        case mpc_start:
			control->current = control->root;
        	if( control->status == mpc_start ) {
                write( control->p_command[fdset][1], "STOP\n", 6 );
        	}
        	else {
				control->status=mpc_start;
				sendplay( fdset, control );
        	}
            break;

        case mpc_play:
        	if( control->status != mpc_start ) {
        		write( control->p_command[fdset][1], "PAUSE\n", 7 );
        		control->status=( mpc_play == control->status )?mpc_idle:mpc_play;
        	}
            break;

        case mpc_prev:
            order=-1;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_next:
            order=1;

            if( ( control->current->key != 0 ) && !( control->current->flags & ( MP_SKPD|MP_CNTD ) ) ) {
                control->current->skipcount++;
                control->current->flags |= MP_SKPD;
                // updateCurrent( control ); - done in STOP handling
            }

            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_doublets:
            write( control->p_command[fdset][1], "STOP\n", 6 );
            progressStart( "Filesystem Cleanup" );

            progressLog( "Checking for doubles..\n" );
            i=dbNameCheck( control->dbname );
            if( i > 0 ) {
            	progressLog( "Deleted %i titles\n", i );
                progressLog( "Restarting player..\n" );
                setProfile( control );
                control->current = control->root;
            }
            else {
            	progressLog( "No titles deleted\n" );
            }

            progressEnd( "Finished Cleanup." );
            sendplay( fdset, control );

            break;

        case mpc_dbclean:
            order=0;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            progressStart( "Database Cleanup" );


            progressLog( "Checking for deleted titles..\n" );
            i=dbCheckExist( control->dbname );

            if( i > 0 ) {
                progressLog( "Removed %i titles\n", i );
                order=1;
            }
            else {
                progressLog( "No titles removed\n" );
            }

            progressLog( "Checking for new titles..\n" );
            i=dbAddTitles( control->dbname, control->musicdir );

            if( i > 0 ) {
                progressLog( "Added %i new titles\n", i );
                order=1;
            }
            else {
                progressLog( "No titles to be added\n" );
            }

            if( 1 == order ) {
                progressLog( "Restarting player..\n" );
                setProfile( control );
                control->current = control->root;
            }

            progressEnd( "Finished Cleanup." );
            sendplay( fdset, control );
            break;

        case mpc_stop:
            order=0;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            // control->status=mpc_idle;
            break;

        case mpc_dnptitle:
            addToFile( control->dnpname, control->current->display, "d=" );
            control->current=removeByPattern( control->current, "d=" );
            order=1;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpalbum:
            addToFile( control->dnpname, control->current->album, "l=" );
            control->current=removeByPattern( control->current, "l=" );
            order=1;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpartist:
            addToFile( control->dnpname, control->current->artist, "a=" );
            control->current=removeByPattern( control->current, "a=" );
            order=1;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpgenre:
            addToFile( control->dnpname, control->current->genre, "g*" );
            control->current=removeByPattern( control->current, "g*" );
            order=1;
            write( control->p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_favtitle:
            addToFile( control->favname, control->current->display, "d=" );
            control->current->flags|=MP_FAV;
            break;

        case mpc_favalbum:
            addToFile( control->favname, control->current->album, "l=" );
            markFavourite( control->current, SL_ALBUM );
            break;

        case mpc_favartist:
            addToFile( control->favname, control->current->artist, "a=" );
            markFavourite( control->current, SL_ARTIST );
            break;

        case mpc_repl:
            write( control->p_command[fdset][1], "JUMP 0\n", 8 );
            break;

        case mpc_quit:
            control->status=mpc_quit;
        	if( control->active != 0 ) {
        		writeConfig( control );
        	}
            gtk_main_quit();
            break;

        case mpc_profile:
            write( control->p_command[fdset][1], "STOP\n", 6 );
            control->status=mpc_idle;
            if( control->dbname[0] == 0 ) {
            	i=control->active;
            	readConfig( control );
            	control->active=i;
            }
            setProfile( control );
            control->current = control->root;
            sendplay( fdset, control );
            break;

        case mpc_shuffle:
            control->root=shuffleTitles(control->root);
            control->current=control->root;
			control->status=mpc_start;
			sendplay( fdset, control );
        	break;

        case mpc_idle:
            // do null
            break;
        }

        control->command=mpc_idle;
        pthread_mutex_unlock( &cmdlock );

    }
    while ( control->status != mpc_quit );

    printver( 2, "Reader stopped\n" );

    return NULL;
}
