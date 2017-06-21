/*
 * player.c
 *
 *  Created on: 26.04.2017
 *      Author: bweber
 */
#include "player.h"
#include "database.h"
#include <string.h>
#include <pthread.h>

pthread_mutex_t cmdlock=PTHREAD_MUTEX_INITIALIZER;

/**
 * implements command queue
 */
void setCommand( struct mpcontrol_t *control, mpcmd cmd ) {
	pthread_mutex_lock( &cmdlock );
	control->command=cmd;
//	pthread_mutex_unlock( &cmdlock );
}

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

void *setProfile( void *data ) {
	char		confdir[MAXPATHLEN]; // = "~/.mixplay";
	char 		*profile;
	struct marklist_t *dnplist=NULL;
	struct marklist_t *favourites=NULL;
	int num;
	struct mpcontrol_t *ctrl;
	int lastver;

	ctrl=(struct mpcontrol_t *)data;

	profile=ctrl->profile[ctrl->active];
	snprintf( confdir, MAXPATHLEN, "%s/.mixplay", getenv("HOME") );

	ctrl->dnpname=falloc( MAXPATHLEN, sizeof(char) );
	snprintf( ctrl->dnpname, MAXPATHLEN, "%s/%s.dnp", confdir, profile );

	ctrl->favname=falloc( MAXPATHLEN, sizeof(char) );
	snprintf( ctrl->favname, MAXPATHLEN, "%s/%s.fav", confdir, profile );
	dnplist=loadList( ctrl->dnpname );
	favourites=loadList( ctrl->favname );

	cleanTitles( ctrl->root );

	ctrl->root=dbGetMusic( ctrl->dbname );
	if( NULL == ctrl->root ) {
		progressLog("Scanning musicdir");
		lastver=getVerbosity();
		if( lastver < 1 ) setVerbosity(1);
		num = dbAddTitles( ctrl->dbname, ctrl->musicdir );
		if( 0 == num ){
			fail( F_FAIL, "No music found at %s!", ctrl->musicdir );
		}
		progressAdd("Added %i titles.", num );
		progressDone( NULL );
		ctrl->root=dbGetMusic( ctrl->dbname );
		if( NULL == ctrl->root ) {
			fail( F_FAIL, "No music found at %s for database %s!\nThis should never happen!",
					ctrl->musicdir,  ctrl->dbname );
		}
		setVerbosity(lastver);
	}
	DNPSkip( ctrl->root, 3 );
	applyDNPlist( ctrl->root, dnplist );
	applyFavourites( ctrl->root, favourites );
	ctrl->root=shuffleTitles(ctrl->root);
	// ctrl->command=mpc_start;
	cleanList( dnplist );
	cleanList( favourites );

	setCommand( ctrl, mpc_start );
	if( ctrl->debug ) progressDone( "Profile set." );
	return NULL;
}

/**
 * checks if the playcount needs to be increased and if the skipcount
 * needs to be decreased. In both cases the updated information is written
 * back into the db.
 */
static void playCount( struct mpcontrol_t *control ) {
	int db;

	if( ( control->current->key != 0 ) && !( control->current->flags & MP_CNTD ) ) {
		control->current->flags |= MP_CNTD; // make sure a title is only counted once per session
		control->current->played++;
		if( !( control->current->flags & MP_SKPD ) && ( control->current->skipped > 0 ) ) {
			control->current->skipped--;
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
	int i, key;
	int invol=100;
	int outvol=100;
	int redraw=0;
	int fdset=0;
	char line[MAXPATHLEN];
	char status[MAXPATHLEN];
	char *a, *t;
	int order=1;
	int intime=0;
	int fade=3;

	printver( 2, "Reader running\n");

	control=(struct mpcontrol_t *)cont;
	
	do {
		FD_ZERO( &fds );
		FD_SET( control->p_status[0][0], &fds );
		FD_SET( control->p_status[1][0], &fds );
		to.tv_sec=0;
		to.tv_usec=100000; // 1/10 second
		i=select( FD_SETSIZE, &fds, NULL, NULL, &to );

		if( i>0 ) redraw=1;
		// drain inactive player
		if( FD_ISSET( control->p_status[fdset?0:1][0], &fds ) ) {
			key=readline(line, 512, control->p_status[fdset?0:1][0]);
			if( ( key > 1 ) && ( outvol > 0 ) && ( line[1] == 'F' ) ){
				outvol--;
				snprintf( line, MAXPATHLEN, "volume %i\n", outvol );
				write( control->p_command[fdset?0:1][1], line, strlen(line) );
			}
		}

		// Interpret mpg123 output and ignore invalid lines
		if( FD_ISSET( control->p_status[fdset][0], &fds ) &&
				( 3 < readline(line, 512, control->p_status[fdset][0]) ) ) {
			// the players may run even if there is no playlist yet
			if( (NULL != control->current ) && ( '@' == line[0] ) ) {
				switch (line[1]) {
				int cmd=0, rem=0;
				case 'R': // startup
					printver( 1, "MPG123 instance %i is running\n", fdset );
				break;
				case 'I': // ID3 info
					// ICY stream info
					if( NULL != strstr( line, "ICY-" ) ) {
						if( NULL != strstr( line, "ICY-NAME: " ) ) {
							strip( control->current->album, line+13, NAMELEN );
						}
						if( NULL != ( a = strstr( line, "StreamTitle") ) ) {
							if( control->current->plnext == control->current )
								fail( F_FAIL, "Messed up playlist!" );
							printver( 3, "%s\n", a );
							a = a + 13;
							*strchr(a, '\'') = '\0';
							strncpy( control->current->plnext->display, control->current->display, MAXPATHLEN );
							strip(control->current->display, a, MAXPATHLEN );
							if( NULL != ( t = strstr( a, " - " ) ) ) {
								*t=0;
								t=t+3;
								strncpy( control->current->artist, a, NAMELEN );
								strncpy( control->current->title, t, NAMELEN );
							}
							else {
								strip(control->current->title, a, NAMELEN );
							}
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
					a=strrchr( line, ' ' );
					rem=atoi(a);
					*a=0;
					a=strrchr( line, ' ' );
					intime=atoi(a);
					// stream play
					if( control->playstream ){
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
							playCount( control );
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
					case 0: // STOP
						// should the playcount be increased?
						playCount( control );
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
			updateUI( control );
		}

		pthread_mutex_trylock( &cmdlock );
		switch( control->command ) {
		case mpc_start:
			control->current = control->root;
			control->status=mpc_play;
			sendplay( control->p_command[fdset][1], control->current);
			break;
		case mpc_play:
			write( control->p_command[fdset][1], "PAUSE\n", 7 );
			control->status=(mpc_play == control->status)?mpc_idle:mpc_play;
			break;
		case mpc_prev:
			order=-1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_next:
			order=1;
			if( ( control->current->key != 0 ) && !(control->current->flags & ( MP_SKPD|MP_CNTD ) ) ) {
				control->current->skipped++;
				control->current->flags |= MP_SKPD;
				// updateCurrent( control ); - done in STOP handling
			}
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_dbclean:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			progressLog( "Database Cleanup" );
			progressAdd( "Checking for new titles..\n" );
			i=dbAddTitles( control->dbname, control->musicdir );
			if( i > 0 ) {
				progressAdd("Added %i new titles\n", i );
				order=1;
			}
			else {
				progressAdd("No titles to be added\n");
			}
			progressAdd( "Checking for deleted titles..\n" );
			i=dbCheckExist( control->dbname );
			if( i > 0 ) {
				progressAdd( "Removed $i titles\n", i );
				order=1;
			}
			else {
				progressAdd( "No titles removed" );
			}
			if( 1 == order ) {
				progressAdd( "Restarting player.." );
				setProfile( control );
				control->current = control->root;
			}
			progressDone( "Finished Cleanup.");
			order=0;
			sendplay( control->p_command[fdset][1], control->current);
			break;


			sendplay( control->p_command[fdset][1], control->current);
			break;
		case mpc_stop:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			control->status=mpc_idle;
			break;
		case mpc_dnptitle:
			addToFile( control->dnpname, control->current->display, "d=" );
			control->current=removeFromPL( control->current, SL_DISPLAY );
			order=1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_dnpalbum:
			addToFile( control->dnpname, control->current->album, "l=" );
			control->current=removeFromPL( control->current, SL_ALBUM );
			order=1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_dnpartist:
			addToFile( control->dnpname, control->current->artist, "a=" );
			control->current=removeFromPL( control->current, SL_ARTIST );
			order=1;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_dnpgenre:
			addToFile( control->dnpname, control->current->genre, "g*" );
			control->current=removeFromPL( control->current, SL_GENRE );
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
			break;
		case mpc_profile:
			// @todo - change profile - probably done in callback itself
			//         not in the Callback! This will disallow updates in the requester!
			break;
		case mpc_stream:
			// @todo - play stream
			break;
		case mpc_idle:
			// do null
			break;
		}
		control->command=mpc_idle;
		pthread_mutex_unlock(&cmdlock);

	} while ( control->status != mpc_quit );

	printver( 2, "Reader stopped\n");

	return NULL;
}
