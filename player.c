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

void setProfile( struct mpcontrol_t *ctrl ) {
	char		confdir[MAXPATHLEN]; // = "~/.mixplay";
	char 		*profile;
	struct marklist_t *dnplist=NULL;
	struct marklist_t *favourites=NULL;
	int num;

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
		progressLog("Adding titles");
		num = dbAddTitles( ctrl->dbname, ctrl->musicdir );
		if( 0 == num ){
			fail( F_FAIL, "No music found at %s!", ctrl->musicdir );
		}
		progressDone("Added %i titles", num );
		ctrl->root=dbGetMusic( ctrl->dbname );
		if( NULL == ctrl->root ) {
			fail( F_FAIL, "No music found at %s for database %s!\nThis should never happen!",
					ctrl->musicdir,  ctrl->dbname );
		}
	}
	DNPSkip( ctrl->root, 3 );
	applyDNPlist( ctrl->root, dnplist );
	applyFavourites( ctrl->root, favourites );
	ctrl->root=shuffleTitles(ctrl->root);
	// ctrl->command=mpc_start;
	setCommand( ctrl, mpc_start ); // @todo: is this deadlock prone during init?
	cleanList( dnplist );
	cleanList( favourites );
}

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
	char tbuf[MAXPATHLEN];
	char status[MAXPATHLEN];
	char *b;
	int db;
	int order=1;
	int intime=0;
	int fade=3;

	control=(struct mpcontrol_t *)cont;
	db=dbOpen( control->dbname );

	while ( control->status != mpc_quit ) {
		FD_ZERO( &fds );
		FD_SET( control->p_status[0][0], &fds );
		FD_SET( control->p_status[1][0], &fds );
		to.tv_sec=0;
		to.tv_usec=100000; // 1/10 second
		i=select( FD_SETSIZE, &fds, NULL, NULL, &to );
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
			if( !(control->current->flags & MP_SKPD  ) ) {
				control->current->skipped++;
				control->current->flags |= MP_SKPD;
				dbPutTitle( db, control->current );
			}
			write( control->p_command[fdset][1], "STOP\n", 6 );
			break;
		case mpc_dbscan:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			progressLog( "Add new titles" );
			i=dbAddTitles( control->dbname, control->musicdir );
			if( i > 0 ) {
				progressDone("Added %i titles\nRestarting player", i );
				setProfile( control );
				control->current = control->root;
			}
			else {
				progressDone("No titles to be added");
			}
			sendplay( control->p_command[fdset][1], control->current);
			break;
		case mpc_dbclean:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			progressLog( "Clean database" );
			i=dbCheckExist( control->dbname );
			if( i > 0 ) {
				progressDone( "Removed $i titles\nRestarting player", i );
				setProfile( control );
				control->current = control->root;
			}
			else {
				progressDone( "No titles removed" );
			}
			sendplay( control->p_command[fdset][1], control->current);
			break;
		case mpc_stop:
			order=0;
			write( control->p_command[fdset][1], "STOP\n", 6 );
			control->status=mpc_idle;
			break;
		case mpc_dnptitle:
//			addToFile( control->dnpname, control->current->title, "t=" );
//			control->current=removeFromPL( control->current, SL_TITLE );
			addToFile( control->dnpname, control->current->title, "d=" );
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
		case mpc_favtitle:
//			addToFile( control->favname, control->current->title, "t=" );
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
			break;
		case mpc_idle:
			// do null
			break;
		}

		pthread_mutex_trylock( &cmdlock );
		control->command=mpc_idle;
		pthread_mutex_unlock(&cmdlock);

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
							if( !( control->current->flags & MP_CNTD ) ) {
								control->current->flags |= MP_CNTD; // make sure a title is only counted once per session
								control->current->played++;
								if( control->current->skipped > 0 ) {
									control->current->skipped--;
								}
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
					case 0: // STOP
						// should the playcount be increased?
						if( !( control->current->flags & ( MP_CNTD | MP_SKPD ) ) ) {
							control->current->flags |= MP_CNTD;
							control->current->played++;
							if( !(control->current->flags & MP_SKPD ) && ( control->current->skipped > 0 ) )
								control->current->skipped--;
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
			gdk_threads_add_idle( updateUI, control );
		}
	} // while(running)

	return NULL;
}
