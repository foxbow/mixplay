#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "gladeutils.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>


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
	struct control_t *control;
	char buf[10];
	control=(struct control_t*)data;
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

	if( control->current->skipped > 0 ) {
		sprintf( buf, "%2i", control->current->skipped );
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
				buf );
	}
	else {
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
				NULL );
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

static void *reader( void *conf ) {
	struct control_t *control;
	struct entry_t *current;
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

	control=(struct control_t *)conf;
	db=dbOpen( control->dbname );

	while ( control->running ) {
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
			if( !(current->flags & ( MP_SKPD | MP_CNTD ) ) ) {
				current->skipped++;
				current->flags |= MP_SKPD;
			}
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case MPCMD_MDNP:
			addToFile( control->dnpname, strrchr( current->path, '/')+1 );
			current=removeFromPL( current, SL_TITLE );
			order=1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case MPCMD_MFAV:
			if( !(current->flags & MP_FAV) ) {
				addToFile( control->favname, strrchr( current->path, '/')+1 );
				current->flags|=MP_FAV;
			}
			break;
		case MPCMD_REPL:
			write( control->p_command[fdset][1], "JUMP 0\n", 8 );
			break;
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
					current = control->root;
					sendplay( control->p_command[fdset][1], current);
				break;
				case 'I': // ID3 info
					// ICY stream info
					if( NULL != strstr( line, "ICY-" ) ) {
						if( NULL != strstr( line, "ICY-NAME: " ) ) {
							strip( current->album, line+13, NAMELEN );
						}
						if( NULL != ( b = strstr( line, "StreamTitle") ) ) {
							b = b + 13;
							*strchr(b, '\'') = '\0';
							if( strlen(current->display) != 0 ) {
								strcpy(tbuf, current->display);
								next=insertTitle( current, tbuf );
								// fix genpathname() from insertTitle
								strip(next->display, tbuf, MAXPATHLEN );
							}
							strip(current->display, b, MAXPATHLEN );
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
							if( !( current->flags & MP_CNTD ) ) {
								current->flags |= MP_CNTD; // make sure a title is only counted once per session
								current->played++;
								if( current->skipped > 0 ) current->skipped--;
								dbPutTitle( db, current );
							}
							next=current->plnext;
							if( next == current ) {
								strcpy( status, "STOP" );
							}
							else {
								current=next;
								// swap player
								fdset=fdset?0:1;
								invol=0;
								outvol=100;
								write( control->p_command[fdset][1], "volume 0\n", 9 );
								sendplay( control->p_command[fdset][1], current);
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
						if ( ( intime > 2 )  && !( current->flags & MP_CNTD ) ) {
							current->flags |= MP_CNTD;
							current->played++;
							if( current->skipped > 0 ) current->skipped--;
							dbPutTitle( db, current );
						}
						next = skipTitles( current, order, 0 );
						if ( ( next == current ) ||
								( ( ( order == 1  ) && ( next == control->root ) ) ||
								  ( ( order == -1 ) && ( next == control->root->plprev ) ) ) ) {
							strcpy( status, "STOP" );
						}
						else {
							if( (order==1) && (next == control->root) ) newCount( control->root );
							current=next;
							sendplay( control->p_command[fdset][1], current);
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
						popUp( 0, "Unknown status!\n %i", cmd );
						break;
					}
					redraw=1;
					break;
				case 'V': // volume reply
					redraw=0;
				break;
				case 'E':
					fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
							current->key,
							current->display,
							current->path);
				break;
				default:
					popUp( 0, "Warning!\n%s", line );
				break;
				} // case line[1]
			} // if line starts with '@'
			// Ignore other mpg123 output
		} // fgets() > 0
		if( redraw ) {
			control->current=current;
			gdk_threads_add_idle( updateUI, control );
		}
	} // while(running)

	return NULL;
}

int main( int argc, char **argv ) {
    MpData     *data;
    GtkBuilder *builder;
    GError     *error = NULL;

    struct control_t control;

    int i;
    int db=0;
	pid_t     pid[2];
	pthread_t tid;
	FILE *fp=NULL;

	char line[MAXPATHLEN];
	char basedir[MAXPATHLEN];
	char confdir[MAXPATHLEN];
	char dbname[MAXPATHLEN]  = "mixplay.db";
	char dnpname[MAXPATHLEN] = "mixplay.dnp";
	char favname[MAXPATHLEN] = "mixplay.fav";
	char config[MAXPATHLEN]  = "mixplay.cnf";

	struct entry_t *root = NULL;

	struct marklist_t *dnplist=NULL;
	struct marklist_t *favourites=NULL;

	// start the player processes
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

	// The handles to talk with the player processes
	muteVerbosity();

	memset( basedir, 0, MAXPATHLEN );

	// load default configuration
	sprintf( confdir, "%s/.mixplay", getenv("HOME") );
	abspath( config, confdir, MAXPATHLEN );
	fp=fopen(config, "r");
	if( NULL != fp ) {
		do {
			i=0;
			fgets( line, MAXPATHLEN, fp );
			if( strlen(line) > 2 ) {
				switch( line[0] ) {
				case 'd':
					strip( dbname, line+1, MAXPATHLEN );
					break;
				case 's':
					strip( basedir, line+1, MAXPATHLEN );
					break;
				case '#':
					break;
				default:
					fail( F_FAIL, "Config error: %s", line );
					break;
				}
			}
		} while( !feof(fp) );
	}

	// if no basedir has been set, use the current directory as default
	if( 0 == strlen(basedir) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
		fail( errno, "Could not get current directory!" );
	}

	// Most simple way to receive a shuffled playlist the
	// main use-case for now
	abspath( dnpname, confdir, MAXPATHLEN );
	dnplist=loadList( dnpname );
	abspath( favname, confdir, MAXPATHLEN );
	favourites=loadList( favname );
	abspath( dbname, confdir, MAXPATHLEN );
	root=dbGetMusic( dbname );
	if( NULL == root ) {
		fail( F_FAIL, "No music/database at %s", dbname );
	}
	root=DNPSkip( root, 3 );
	root=applyDNPlist( root, dnplist );
	applyFavourites( root, favourites );
	root=shuffleTitles(root);

	control.root=root;
	control.dbname=dbname;
	control.favname=favname;
	control.dnpname=dnpname;
	control.stream=0;
	strcpy( control.playtime, "00:00" );
	strcpy( control.remtime, "00:00" );
	control.percent=0;
	control.status=MPCMD_PLAY;

    /* Init GTK+ */
    gtk_init( &argc, &argv );

    /* Create new GtkBuilder object */
    builder = gtk_builder_new();
    if( ! gtk_builder_add_from_file( builder, UI_FILE, &error ) )
    {
        g_warning( "%s", error->message );
        g_free( error );
        return( 1 );
    }

    /* Allocate data structure */
    data = g_slice_new( MpData );

    /* Get objects from UI */
#define GW( name ) MP_GET_WIDGET( builder, name, data )
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
#undef GW
//	g_object_ref(control->widgets->down );
//	g_object_ref(control->widgets->down );


	control.widgets = data;
	g_object_ref(control.widgets->play );
	g_object_ref(control.widgets->pause );

	pthread_create( &tid, NULL, reader, &control );

	// attach data to the buttons, so they can communicate with the player
	g_object_set_qdata( (GObject *)data->button_play, g_quark_from_static_string("control"), &control );
	g_object_set_qdata( (GObject *)data->button_prev, g_quark_from_static_string("control"), &control );
	g_object_set_qdata( (GObject *)data->button_next, g_quark_from_static_string("control"), &control );
	g_object_set_qdata( (GObject *)data->button_dnp, g_quark_from_static_string("control"), &control );
	g_object_set_qdata( (GObject *)data->button_fav, g_quark_from_static_string("control"), &control );
	g_object_set_qdata( (GObject *)data->button_replay, g_quark_from_static_string("control"), &control );

    /* Connect signals */
    gtk_builder_connect_signals( builder, data );

    /* Destroy builder, since we don't need it anymore */
    g_object_unref( G_OBJECT( builder ) );

    /* Show window. All other widgets are automatically shown by GtkBuilder */
    gtk_widget_show( data->mixplay_main );

    /* Start main loop */
    gtk_main();

    pthread_cancel( tid );
	kill( pid[0], SIGTERM);
	kill( pid[1], SIGTERM );

    /* Free any allocated data */
    g_slice_free( MpData, data );

	dbClose( db );

	return( 0 );
}
