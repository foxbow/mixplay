#define DEBUG

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

volatile struct mpcontrol_t *mpcontrol;

/**
 * make mpeg123 play the given title
 */
static void sendplay( int fd, struct entry_t *song ) {
	char line[MAXPATHLEN]="load ";
	strncat( line, song->path, MAXPATHLEN );
	strncat( line, "\n", MAXPATHLEN );
	if ( write( fd, line, MAXPATHLEN ) < strlen(line) ) {
		fail( F_FAIL, "Could not write\n%s", line );
	}
}

static int updateUI( void *data ) {
	struct mpcontrol_t *control;
	char buff[MAXPATHLEN];
	control=(struct mpcontrol_t*)data;

	if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
		gtk_label_set_text( GTK_LABEL( control->widgets->title_current ),
				control->current->title );
		gtk_label_set_text( GTK_LABEL( control->widgets->artist_current ),
				control->current->artist );
		gtk_label_set_text( GTK_LABEL( control->widgets->album_current ),
				control->current->album );
		gtk_label_set_text( GTK_LABEL( control->widgets->genre_current ),
				control->current->genre );
		gtk_label_set_text( GTK_LABEL( control->widgets->displayname_prev ),
				control->current->plprev->display );
		gtk_label_set_text( GTK_LABEL( control->widgets->displayname_next ),
				control->current->plnext->display );
#ifdef DEBUG
		snprintf( buff, MAXPATHLEN, "[%i]", control->current->played );
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ),
				buff );
#endif
	}
	if( NULL != control->current ) {
		gtk_window_set_title ( GTK_WINDOW( control->widgets->mixplay_main ),
							  control->current->display );
	}

	if( MPCMD_PLAY == control->status ) {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_play ),
				control->widgets->pause );
	}
	else {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_play ),
				control->widgets->play );
	}

#ifdef DEBUG
	if( control->current->skipped > 0 ) {
		sprintf( buff, "%2i", control->current->skipped );
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
				buff );
	}
	else {
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
				NULL );
	}
#endif

	if( control->current->skipped > 2 ) {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->noentry );
	}
	else {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->down );
	}

/** skipcontrol is off
	if( control->percent < 5 ) {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->skip );
	}
	else {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->down );
	}
**/
	// other settings
	gtk_label_set_text( GTK_LABEL( control->widgets->played ),
			control->playtime );
	gtk_label_set_text( GTK_LABEL( control->widgets->remain ),
			control->remtime );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( control->widgets->progress),
			control->percent/100.0 );
	gtk_widget_set_sensitive( control->widgets->button_fav, ( !(control->current->flags & MP_FAV) ) );

	return 0;
}

static void setProfile( struct mpcontrol_t *ctrl ) {
	char		confdir[MAXPATHLEN]; // = "~/.mixplay";
	char 		*profile;
	struct marklist_t *dnplist=NULL;
	struct marklist_t *favourites=NULL;

	profile=ctrl->profile[ctrl->active];
	snprintf( confdir, MAXPATHLEN, "%s/.mixplay", getenv("HOME") );

	ctrl->dnpname=calloc( MAXPATHLEN, sizeof(char) );
	snprintf( ctrl->dnpname, MAXPATHLEN, "%s/%s.dnp", confdir, profile );

	ctrl->favname=calloc( MAXPATHLEN, sizeof(char) );
	snprintf( ctrl->favname, MAXPATHLEN, "%s/%s.fav", confdir, profile );
	dnplist=loadList( ctrl->dnpname );
	favourites=loadList( ctrl->favname );

	cleanTitles( ctrl->root );

	ctrl->root=dbGetMusic( ctrl->dbname );
	if( NULL == ctrl->root ) {
		fail( F_FAIL, "No music/database at %s", ctrl->dbname );
	}
	ctrl->root=DNPSkip( ctrl->root, 3 );
	ctrl->root=applyDNPlist( ctrl->root, dnplist );
	ctrl->root=applyFavourites( ctrl->root, favourites );
	ctrl->root=shuffleTitles(ctrl->root);

	cleanList( dnplist );
	cleanList( favourites );
}

static void loadConfig( struct mpcontrol_t *config ) {
	GKeyFile	*keyfile;
	GError		*error=NULL;
	char		confdir[MAXPATHLEN]; // = "~/.mixplay";
	char		conffile[MAXPATHLEN]; //  = "mixplay.conf";
	int			res;

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
	if( ! g_key_file_load_from_file( keyfile, conffile, G_KEY_FILE_NONE, &error ) ) {
		res = gtk_dialog_run (GTK_DIALOG ( config->widgets->fileselect ));
		if (res == GTK_RESPONSE_ACCEPT) {
		    config->musicdir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( config->widgets->fileselect ) );
		    error=NULL;
		    if( !g_key_file_save_to_file( keyfile, conffile, &error ) ) {
		    	fail( F_FAIL, "Could not write configuration!\n%s", error->message );
		    }
		}
		else {
			fail( F_FAIL, "No Music directory chosen!" );
		}
	}
	else {
		config->musicdir=g_key_file_get_string( keyfile, "mixplay", "musicdir", &error );
		if( NULL == config->musicdir ) {
			fail( F_FAIL, "No music dir set in configuration!\n%s", error->message );
		}
		config->profile =g_key_file_get_string_list( keyfile, "mixplay", "profiles", &config->profiles, &error );
		if( NULL == config->profile ) {
			error=NULL;
			config->profile[0]=calloc( 8, sizeof( char ) );
			strcpy( config->profile[0], "mixplay" );
			config->active=0;
		}
		else {
			config->active  =g_key_file_get_uint64( keyfile, "mixplay", "active", &error );
		}
		config->stream=g_key_file_get_string_list( keyfile, "mixplay", "streams", &config->streams, &error );
		if( NULL != config->stream ) {
			config->sname =g_key_file_get_string_list( keyfile, "mixplay", "snames", &config->streams, &error );
			if( error ) {
				fail( F_FAIL, "Streams set but no names!\n%s", error->message );
			}
		}
	}
}

/**
 * the control thread to communicate with the mpg123 processes
 */
static void *reader( void *cont ) {
	struct mpcontrol_t *control;
//	struct entry_t *current;
	struct entry_t *next;
	fd_set fds;
	struct timeval to;
	int i, key;
	int invol=100;
	int outvol=100;
	int redraw=0;
	int fdset=0;
	char line[MAXPATHLEN];
	char tbuf[MAXPATHLEN];
	char status[MAXPATHLEN];
	char *b;
	int db;
	int order=1;
	int intime=0;
	int fade=3;

	control=(struct mpcontrol_t *)cont;
	db=dbOpen( control->dbname );

	while ( control->status != MPCMD_QUIT ) {
		FD_ZERO( &fds );
		FD_SET( control->p_status[0][0], &fds );
		FD_SET( control->p_status[1][0], &fds );
		to.tv_sec=0;
		to.tv_usec=100000; // 1/10 second
		i=select( FD_SETSIZE, &fds, NULL, NULL, &to );
		switch( control->command ) {
		case MPCMD_PLAY:
			write( control->p_command[fdset][1], "PAUSE\n", 7 );
			control->status ^= MPCMD_PLAY;
			break;
		case MPCMD_PREV:
			order=-1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case MPCMD_NEXT:
			order=1;
			if( !(control->current->flags & ( MP_SKPD | MP_CNTD ) ) ) {
				control->current->skipped++;
				control->current->flags |= MP_SKPD;
			}
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case MPCMD_DBSCAN:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			control->status=MPCMD_DBSCAN;
			dbAddTitles( control->dbname, control->musicdir );
			contReq("OK - restart");
			mpcontrol->command=MPCMD_QUIT;
			gtk_main_quit ();
			break;
		case MPCMD_DBCLEAN:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			control->status=MPCMD_DBCLEAN;
			dbCheckExist( control->dbname );
			contReq( "OK - restart" );
			mpcontrol->command=MPCMD_QUIT;
			gtk_main_quit ();
			break;
		case MPCMD_STOP:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			control->status=MPCMD_IDLE;
			break;
		case MPCMD_MDNP:
			addToFile( control->dnpname, control->current->display, "d=" );
			control->current=removeFromPL( control->current, SL_TITLE );
			order=1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case MPCMD_MFAV:
			if( !(control->current->flags & MP_FAV) ) {
				addToFile( control->favname, control->current->display, "d=" );
				control->current->flags|=MP_FAV;
			}
			break;
		case MPCMD_REPL:
			write( control->p_command[fdset][1], "JUMP 0\n", 8 );
			break;
		case MPCMD_QUIT:
			control->status=MPCMD_QUIT;
			break;
		}
		control->command=MPCMD_IDLE;

		if( i>0 ) redraw=1;
		// drain inactive player
		if( FD_ISSET( control->p_status[fdset?0:1][0], &fds ) ) {
			key=readline(line, 512, control->p_status[fdset?0:1][0]);
			if( ( key > 0 ) && ( outvol > 0 ) && ( line[1] == 'F' ) ){
				outvol--;
				snprintf( line, MAXPATHLEN, "volume %i\n", outvol );
				write( control->p_command[fdset?0:1][1], line, strlen(line) );
			}
		}

		// Interpret mpg123 output and ignore invalid lines
		if( FD_ISSET( control->p_status[fdset][0], &fds ) &&
				( 3 < readline(line, 512, control->p_status[fdset][0]) ) ) {
			if( '@' == line[0] ) {
				switch (line[1]) {
				int cmd=0, rem=0;
				case 'R': // startup
					control->current = control->root;
					sendplay( control->p_command[fdset][1], control->current);
				break;
				case 'I': // ID3 info
					// ICY stream info
					if( NULL != strstr( line, "ICY-" ) ) {
						if( NULL != strstr( line, "ICY-NAME: " ) ) {
							strip( control->current->album, line+13, NAMELEN );
						}
						if( NULL != ( b = strstr( line, "StreamTitle") ) ) {
							b = b + 13;
							*strchr(b, '\'') = '\0';
							if( strlen(control->current->display) != 0 ) {
								strcpy(tbuf, control->current->display);
								next=insertTitle( control->current, tbuf );
								// fix genpathname() from insertTitle
								strip(next->display, tbuf, MAXPATHLEN );
							}
							strip(control->current->display, b, MAXPATHLEN );
						}
					}
					// standard mpg123 info
					else if ( strstr(line, "ID3") != NULL ) {
						// ignored
					}
				break;
				case 'T': // TAG reply
					fail( F_FAIL, "Got TAG reply!" );
					break;
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
					b=strrchr( line, ' ' );
					rem=atoi(b);
					*b=0;
					b=strrchr( line, ' ' );
					intime=atoi(b);
					// stream play
					if( control->stream ){
						if( intime/60 < 60 ) {
							sprintf(status, "%i:%02i PLAYING", intime/60, intime%60 );
						}
						else {
							sprintf(status, "%i:%02i:%02i PLAYING", intime/3600, (intime%3600)/60, intime%60 );
						}
					}
					// file play
					else {
						control->percent=(100*intime)/(rem+intime);
						sprintf( control->playtime, "%02i:%02i", intime/60, intime%60 );
						sprintf( control->remtime, "%02i:%02i", rem/60, rem%60 );
						if( rem <= fade ) {
							// should the playcount be increased?
							// !MP_CNTD - title has not been counted yet
							if( !( control->current->flags & MP_CNTD ) ) {
								control->current->flags |= MP_CNTD; // make sure a title is only counted once per session
								control->current->played++;
								if( control->current->skipped > 0 ) control->current->skipped--;
								dbPutTitle( db, control->current );
							}
							next=control->current->plnext;
							if( next == control->current ) {
								strcpy( status, "STOP" );
							}
							else {
								control->current=next;
								// swap player
								fdset=fdset?0:1;
								invol=0;
								outvol=100;
								write( control->p_command[fdset][1], "volume 0\n", 9 );
								sendplay( control->p_command[fdset][1], control->current);
							}
						}
						if( invol < 100 ) {
							invol++;
							snprintf( line, MAXPATHLEN, "volume %i\n", invol );
							write( control->p_command[fdset][1], line, strlen(line) );
						}

					}
					redraw=1;
				break;
				case 'P': // Player status
					cmd = atoi(&line[3]);
					switch (cmd) {
					case 0:
						// should the playcount be increased?
						// search  - partymode
						// mix     - playcount relevant
						// intime  - has been played for more than 2 secs
						// usedb   - playcount is persistent
						// !MP_CNTD - title has not been counted yet
						if ( ( intime > 2 )  && !( control->current->flags & MP_CNTD ) ) {
							control->current->flags |= MP_CNTD;
							control->current->played++;
							if( control->current->skipped > 0 ) control->current->skipped--;
							dbPutTitle( db, control->current );
						}
						next = skipTitles( control->current, order, 0 );
						if ( ( next == control->current ) ||
								( ( ( order == 1  ) && ( next == control->root ) ) ||
								  ( ( order == -1 ) && ( next == control->root->plprev ) ) ) ) {
							strcpy( status, "STOP" );
						}
						else {
							if( (order==1) && (next == control->root) ) newCount( control->root );
							control->current=next;
							sendplay( control->p_command[fdset][1], control->current);
						}
						order=1;
						break;
					case 1:
						strcpy( status, "PAUSE" );
						break;
					case 2:
						strcpy( status, "PLAYING" );
						break;
					default:
						fail( F_WARN, "Unknown status!\n %i", cmd );
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
							control->current->path);
				break;
				default:
					fail( F_WARN, "Warning!\n%s", line );
				break;
				} // case line[1]
			} // if line starts with '@'
			// Ignore other mpg123 output
		} // fgets() > 0
		if( redraw ) {
//			control->current=current;
			gdk_threads_add_idle( updateUI, control );
		}
	} // while(running)

	return NULL;
}

/**
 * should be triggered after the app window is realized
 */
int initAll( void *data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t*)data;
	loadConfig( control );

	control->root=NULL;

	control->stream=0;
	strcpy( control->playtime, "00:00" );
	strcpy( control->remtime, "00:00" );
	control->percent=0;
	control->status=MPCMD_PLAY;
	setProfile( control );

	pthread_create( &control->rtid, NULL, reader, control );

	return 0;
}

int main( int argc, char **argv ) {
    GtkBuilder *builder;
    GError     *error = NULL;

    struct mpcontrol_t control;

    int			i;
    int 		db=0;
	pid_t		pid[2];

	mpcontrol=&control;

	char basedir[MAXPATHLEN]; // = ".";
	// if no basedir has been set, use the current directory as default
	if( 0 == strlen(basedir) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
		fail( errno, "Could not get current directory!" );
	}

#ifdef DEBUG
	setVerbosity(1);
#else
	muteVerbosity();
#endif
	memset( basedir, 0, MAXPATHLEN );

    /* Init GTK+ */
    gtk_init( &argc, &argv );

    /* Create new GtkBuilder object */
    builder = gtk_builder_new();
    if( ! gtk_builder_add_from_file( builder, "gmixplay.glade", &error ) )
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
	GW( button_profile );
	GW( mp_popup );
	GW( popupText );
	GW( button_popupOkay );
	GW( fileselect );
#undef GW

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
    control.status=MPCMD_QUIT;

    pthread_join( control.rtid, NULL );
	kill( pid[0], SIGTERM);
	kill( pid[1], SIGTERM );

    /* Free any allocated data */
    g_slice_free( MpData, control.widgets );
    cleanTitles( control.root );
	dbClose( db );

	return( 0 );
}
