/*
 * player.c
 *
 *  Created on: 26.04.2017
 *      Author: bweber
 */
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <alsa/asoundlib.h>
#include <unistd.h>

#include "utils.h"
#include "database.h"
#include "player.h"
#include "config.h"

static pthread_mutex_t cmdlock=PTHREAD_MUTEX_INITIALIZER;

/**
 * adjusts the master volume
 * if volume is 0 the current volume is returned without changing it
 * otherwise it's changed by 'volume'
 * if ALSA does not work or the current card cannot be selected -1 is returned
 */
static long adjustVolume( long volume ) {
    long min, max;
    snd_mixer_t *handle;
    snd_mixer_elem_t *elem;
    snd_mixer_selem_id_t *sid;
    long retval = 0;
    char *channel;
    mpconfig *config;
    config=getConfig();
    channel=config->channel;

    if( channel == NULL || strlen( channel ) == 0 ) {
    	config->volume=-1;
    	return -1;
    }

    snd_mixer_open(&handle, 0);
    if( handle == NULL ) {
    	addMessage( 1, "No ALSA support" );
    	config->volume=-1;
    	return -1;
    }

    snd_mixer_attach(handle, "default");
    snd_mixer_selem_register(handle, NULL, NULL);
    snd_mixer_load(handle);

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, channel );
    elem = snd_mixer_find_selem(handle, sid);
    if( elem == NULL) {
    	addMessage( 0, "Can't find channel %s!", handle );
        snd_mixer_close(handle);
    	config->volume=-1;
    	return -1;
    }

	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_MONO, &retval );
	retval=(( retval * 100 ) / max)+1;

	retval+=volume;
	if( retval < 0 ) retval=0;
	if( retval > 100 ) retval=100;
	snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
	snd_mixer_selem_set_playback_volume_all(elem, ( retval * max ) / 100);
	config->volume=retval;

    snd_mixer_close(handle);

    return retval;
}

/**
 * sets the given stream
 */
static void setStream( struct mpcontrol_t *control, const char* stream, const char *name ) {
	control->root=cleanTitles( control->root );
    control->root=insertTitle( NULL, stream );
    strncpy( control->root->title, name, NAMELEN );
    insertTitle( control->root, control->root->title );
    addToPL( control->root->dbnext, control->root );
    strncpy( control->root->dbnext->display, "---", 3 );
    control->current=control->root;
    addMessage( 1, "Play Stream %s (%s)", name, stream );
}

/**
 * sends a command to the player
 * also makes sure that commands are queued
 */
void setPCommand(  mpcmd cmd ) {
    pthread_mutex_lock( &cmdlock );
    getConfig()->command=cmd;
}

/**
 * parse arguments given to the application
 * also handles playing of a single file, a directory, a playlist or an URL
 * returns 0 if nothing was recognized
 */
int setArgument( const char *arg ) {
    char line [MAXPATHLEN];
    int  i;
    mpconfig *control=getConfig();

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
        control->current=control->root;
        setStream( control, line, "Waiting for stream info..." );
        return 1;
    }
    else if( endsWith( arg, ".mp3" ) ) {
        /* play single song... */
    	control->root=cleanTitles( control->root );
        control->root=insertTitle( NULL, arg );
        control->current=control->root;
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
        control->current=control->root;
        return 4;
    }

    return 0;
}

/**
 * make mpeg123 play the given title
 */
static void sendplay( int fdset, struct mpcontrol_t *control ) {
    char line[MAXPATHLEN]="load ";
    assert( control->current != NULL );

    strncat( line, control->current->path, MAXPATHLEN );
    strncat( line, "\n", MAXPATHLEN );

    if ( write( fdset, line, MAXPATHLEN ) < strlen( line ) ) {
        fail( F_FAIL, "Could not write\n%s", line );
    }

    addMessage( 2, "CMD: %s", line );
}

/**
 * sets the current profile
 * This is thread-able to have progress information on startup!
 */
void *setProfile( void *data ) {
    char		confdir[MAXPATHLEN]; /* = "~/.mixplay"; */
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
            addMessage( 1, "Scanning musicdir" );
            lastver=getVerbosity();

            if( lastver < 1 ) {
                setVerbosity( 1 );
            }

            num = dbAddTitles( control->dbname, control->musicdir );

            if( 0 == num ) {
                fail( F_FAIL, "No music found at %s!", control->musicdir );
            }

            addMessage( 1, "Added %i titles.", num );
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

        addMessage( 1, "Profile set to %s.", profile );
    }
    else {
    	fail( F_FAIL, "Neither profile nor stream set!" );
    }

    /* if we're not in player context, start playing automatically */
    if( pthread_mutex_trylock( &cmdlock ) == 0 ){
    	addMessage( 1, "Autoplay" );
        control->command=mpc_start;
    }

    if( active != control->active ) {
    	control->changed = -1;
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
        control->current->flags |= MP_CNTD; /* make sure a title is only counted once per session */
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
    struct mpcontrol_t	*control;
    struct entry_t 		*next;
    fd_set				fds;
    struct timeval		to;
    int64_t	i, key;
    int		invol=80;
    int		outvol=80;
    int 	redraw=0;
    int 	fdset=0;
    char 	line[MAXPATHLEN];
    char 	*a, *t;
    int 	order=1;
    int 	intime=0;
    int 	fade=3;
    int 	p_status[2][2];			/* status pipes to mpg123 */
    int 	p_command[2][2];		/* command pipes to mpg123 */
    pid_t	pid[2];

    control=( struct mpcontrol_t * )cont;
    assert( control->fade < 2 );

    addMessage( 2, "Reader starting" );

	if( control->fade == 0 ) {
        addMessage( 1, "No crossfading" );
    	fade=0;
    }

    /* start the needed mpg123 instances */
    /* start the player processes */
    /* these may wait in the background until */
    /* something needs to be played at all */
    for( i=0; i <= control->fade; i++ ) {
        /* create communication pipes */
        pipe( p_status[i] );
        pipe( p_command[i] );
        pid[i] = fork();
        /* todo: consider spawn() instead
         * https://unix.stackexchange.com/questions/252901/get-output-of-posix-spawn
         */

        if ( 0 > pid[i] ) {
            fail( errno, "could not fork" );
        }

        /* child process */
        if ( 0 == pid[i] ) {
            addMessage(  2, "Starting player %i", i+1 );

            if ( dup2( p_command[i][0], STDIN_FILENO ) != STDIN_FILENO ) {
                fail( errno, "Could not dup stdin for player %i", i+1 );
            }

            if ( dup2( p_status[i][1], STDOUT_FILENO ) != STDOUT_FILENO ) {
                fail( errno, "Could not dup stdout for player %i", i+1 );
            }

            /* this process needs no pipes */
            close( p_command[i][0] );
            close( p_command[i][1] );
            close( p_status[i][0] );
            close( p_status[i][1] );
            /* Start mpg123 in Remote mode */
            execlp( "mpg123", "mpg123", "-R", "2> &1", NULL );
            fail( errno, "Could not exec mpg123" );
        }

        close( p_command[i][0] );
        close( p_status[i][1] );
    }

    /* check if we can control the system's volume */
    control->volume=adjustVolume( 0 );
    if( control->volume != -1  ) {
    	addMessage(  1, "Hardware volume level is %i%%", control->volume );
    }
    else {
    	/* control mpg123 volume instead - not recommended */
    	addMessage( 0, "No hardware volume control!" );
    	control->channel=NULL;
    }

    /* main loop */
    do {
    	redraw=0;
        FD_ZERO( &fds );
        for( i=0; i<=control->fade; i++ ) {
        	FD_SET( p_status[i][0], &fds );
        }
        to.tv_sec=0;
        to.tv_usec=100000; /* 1/10 second */
        i=select( FD_SETSIZE, &fds, NULL, NULL, &to );

        if( i > 0 ) {
        	redraw=1;
        }

        /* drain inactive player */
        if( control->fade && FD_ISSET( p_status[fdset?0:1][0], &fds ) ) {
            key=readline( line, 512, p_status[fdset?0:1][0] );

            if( key > 2 ) {
				if( '@' == line[0] ) {
	        		if( ( 'F' != line[1] ) && ( 'V' != line[1] ) && ( 'I' != line[1] )) {
	        			addMessage(  2, "P- %s", line );
	        		}

					switch ( line[1] ) {
					case 'R': /* startup */
						addMessage(  1, "MPG123 background instance is up" );
						break;
					case 'E':
						fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
							  control->current->key,
							  control->current->display,
							  control->current->path );
						break;
					case 'F':
						if( outvol > 0 ) {
							outvol--;
							snprintf( line, MAXPATHLEN, "volume %i\n", outvol );
							write( p_command[fdset?0:1][1], line, strlen( line ) );
						}
						break;
					}
				}
				else {
					addMessage(  1, "OFF123: %s", line );
				}
            }
        }

        /* Interpret mpg123 output and ignore invalid lines */
        if( FD_ISSET( p_status[fdset][0], &fds ) &&
                ( 3 < readline( line, 512, p_status[fdset][0] ) ) ) {
        	if( '@' == line[0] ) {
        		/* Don't print volume, progress and MP3Tag messages */
        		if( ( 'F' != line[1] ) && ( 'V' != line[1] ) && ( 'I' != line[1] ) ) {
        			addMessage( 2, "P+ %s", line );
        		}
                switch ( line[1] ) {
                    int cmd;
					float rem;

                case 'R': /* startup */
                    addMessage(  1, "MPG123 foreground instance is up" );
                    break;

                case 'I': /* ID3 info */

                    /* ICY stream info */
                    if( NULL != strstr( line, "ICY-" ) ) {
                        if( NULL != strstr( line, "ICY-NAME: " ) ) {
                            strip( control->current->album, line+13, NAMELEN );
                        }

                        if( NULL != ( a = strstr( line, "StreamTitle" ) ) ) {
                            if( control->current->plnext == control->current ) {
                                fail( F_FAIL, "Messed up playlist!" );
                            }

                            addMessage( 3, "%s", a );
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

                    break;

                case 'T': /* TAG reply */
                    fail( F_FAIL, "Got TAG reply!" );
                    break;

                case 'J': /* JUMP reply */
                    redraw=0;
                    break;

                case 'S': /* Status message after loading a song (stream info) */
                    redraw=0;
                    break;

                case 'F': /* Status message during playing (frame info) */
                    /* $1   = framecount (int)
                     * $2   = frames left this song (int)
                     * in  = seconds (float)
                     * rem = seconds left (float)
                     */
                	a=strrchr( line, ' ' );
                    rem=strtof( a, NULL );
                    *a=0;
                    a=strrchr( line, ' ' );
                    intime=atoi( a );

                    if( intime/60 < 60 ) {
						sprintf( control->playtime, "%02i:%02i", intime/60, intime%60 );
					}
					else {
						sprintf( control->playtime, "%02i:%02i:%02i", intime/3600, ( intime%3600 )/60, intime%60 );
					}
                    /* file play */
                    if( control->playstream == 0 ) {
                        control->percent=( 100*intime )/( rem+intime );
                        sprintf( control->remtime, "%02i:%02i", (int)rem/60, (int)rem%60 );

                        if( rem <= fade ) {
                            /* should the playcount be increased? */
                            playCount( control );
                            next=control->current->plnext;

                            if( next == control->current ) {
                            	control->status=mpc_idle; /* STOP */
                            }
                            else {
                                control->current=next;
                                /* swap player if we want to fade */
                                if( control->fade ) {
                                	fdset=fdset?0:1;
                                	invol=0;
                                	outvol=100;
                                	write( p_command[fdset][1], "volume 0\n", 9 );
                                }
                                sendplay( p_command[fdset][1], control );
                            }
                        }

                        if( invol < 100 ) {
                            invol++;
                            snprintf( line, MAXPATHLEN, "volume %i\n", invol );
                            write( p_command[fdset][1], line, strlen( line ) );
                        }

                    }

                    redraw=1;
                    break;

                case 'P': /* Player status */
                    cmd = atoi( &line[3] );

                    switch ( cmd ) {
                    case 0: /* STOP */
                    	/* player was not yet fully initialized, start again */
                    	if( control->status != mpc_play ) {
                			sendplay( p_command[fdset][1], control );
                    	}
                        /* should the playcount be increased? */
                    	else if( control->playstream == 0 ) {
							playCount( control );
							next = skipTitles( control->current, order, 0 );

							if ( ( next == control->current ) ||
									( ( ( order == 1  ) && ( next == control->root ) ) ||
									  ( ( order == -1 ) && ( next == control->root->plprev ) ) ) ) {
								control->status=mpc_idle; /* stop */
							}
							else {
								if( ( order==1 ) && ( next == control->root ) ) {
									control->root=shuffleTitles( control->root );
									next=control->root;
								}

								control->current=next;
								sendplay( p_command[fdset][1], control );
							}

							order=1;
                    	}
                    	else {
                    		control->status=mpc_idle;
                    	}
                        break;

                    case 1: /* PAUSE */
                    	control->status=mpc_idle;
                        break;

                    case 2:  /* PLAY */
                    	if( control->status == mpc_start ) {
                        	addMessage(  2, "Playing profile #%i", control->active );
                    	}
                    	control->status=mpc_play;
                        break;

                    default:
                        addMessage( 0, "Unknown status %i!\n%s", cmd, line );
                        break;
                    }

                    redraw=1;
                    break;

                case 'V': /* volume reply */
                    redraw=0;
                    break;

                case 'E':
                    fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
                          control->current->key,
                          control->current->display,
                          control->current->path );
                    break;

                default:
                    addMessage( 0, "Warning!\n%s", line );
                    break;
                } /* case line[1] */
            } /* if line starts with '@' */
            else {
            	addMessage( 0, "MPG123: %s", line );
            }
        } /* fgets() > 0 */

        /* notify UI that something has changed */
        if( redraw ) {
            updateUI( control );
        }

        pthread_mutex_trylock( &cmdlock );

        if( control->command != mpc_idle ) {
        	addMessage(  2, "MPC %s", mpcString(control->command) );
        }
        switch( control->command ) {
        case mpc_start:
			control->current = control->root;
        	if( control->status == mpc_start ) {
                write( p_command[fdset][1], "STOP\n", 6 );
        	}
        	else {
				control->status=mpc_start;
				sendplay( p_command[fdset][1], control );
        	}
            break;

        case mpc_play:
        	if( control->status != mpc_start ) {
        		write( p_command[fdset][1], "PAUSE\n", 7 );
        		control->status=( mpc_play == control->status )?mpc_idle:mpc_play;
        	}
            break;

        case mpc_prev:
            order=-1;
            write( p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_next:
            order=1;

            if( ( control->current->key != 0 ) && !( control->current->flags & ( MP_SKPD|MP_CNTD ) ) ) {
                control->current->skipcount++;
                control->current->flags |= MP_SKPD;
                /* updateCurrent( control ); - done in STOP handling */
            }

            write( p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_doublets:
            write( p_command[fdset][1], "STOP\n", 6 );
            progressStart( "Filesystem Cleanup" );

            addMessage( 0, "Checking for doubles.." );
            i=dbNameCheck( control->dbname );
            if( i > 0 ) {
            	addMessage( 0, "Deleted %i titles", i );
                addMessage( 0, "Restarting player.." );
                setProfile( control );
                control->current = control->root;
            }
            else {
            	addMessage( 0, "No titles deleted" );
            }

            progressEnd( "Finished Cleanup." );
            sendplay( p_command[fdset][1], control );

            break;

        case mpc_dbclean:
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
            progressStart( "Database Cleanup" );


            addMessage( 0, "Checking for deleted titles.." );
            i=dbCheckExist( control->dbname );

            if( i > 0 ) {
                addMessage( 0, "Removed %i titles", i );
                order=1;
            }
            else {
                addMessage( 0, "No titles removed" );
            }

            addMessage( 0, "Checking for new titles.." );
            i=dbAddTitles( control->dbname, control->musicdir );

            if( i > 0 ) {
                addMessage( 0, "Added %i new titles", i );
                order=1;
            }
            else {
                addMessage( 0, "No titles to be added" );
            }

            if( 1 == order ) {
                addMessage( 0, "Restarting player.." );
                setProfile( control );
                control->current = control->root;
            }

            progressEnd( "Finished Cleanup." );
            sendplay( p_command[fdset][1], control );
            break;

        case mpc_stop:
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
            /* control->status=mpc_idle; */
            break;

        case mpc_dnptitle:
            addToFile( control->dnpname, control->current->display, "d=" );
            control->current=removeByPattern( control->current, "d=" );
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpalbum:
            addToFile( control->dnpname, control->current->album, "l=" );
            control->current=removeByPattern( control->current, "l=" );
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpartist:
            addToFile( control->dnpname, control->current->artist, "a=" );
            control->current=removeByPattern( control->current, "a=" );
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
            break;

        case mpc_dnpgenre:
            addToFile( control->dnpname, control->current->genre, "g*" );
            control->current=removeByPattern( control->current, "g*" );
            order=0;
            write( p_command[fdset][1], "STOP\n", 6 );
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
            write( p_command[fdset][1], "JUMP 0\n", 8 );
            break;

        case mpc_QUIT:
        case mpc_quit:
            control->status=mpc_quit;
        	if( control->changed != 0 ) {
        		writeConfig( NULL );
        	}
            /* The player does not know about the main App so anything setting mcp_quit
             * MUST make sure that the main app terminates as well ! */
            break;

        case mpc_profile:
            write( p_command[fdset][1], "STOP\n", 6 );
            control->status=mpc_idle;
            if( control->dbname[0] == 0 ) {
            	i=control->active;
            	readConfig( );
            	control->active=i;
            }
            setProfile( control );
            control->current = control->root;
            sendplay( p_command[fdset][1], control );
            break;

        case mpc_shuffle:
            control->root=shuffleTitles(control->root);
            control->current=control->root;
			control->status=mpc_start;
			sendplay( p_command[fdset][1], control );
        	break;

        case mpc_ivol:
        	adjustVolume( +VOLSTEP );
        	break;

        case mpc_dvol:
        	adjustVolume( -VOLSTEP );
        	break;

        case mpc_bskip:
            write( p_command[fdset][1], "JUMP -64\n", 10 );
            break;

        case mpc_fskip:
            write( p_command[fdset][1], "JUMP +64\n", 10 );
            break;

        case mpc_idle:
            /* do null */
            break;
        }

        control->command=mpc_idle;
        pthread_mutex_unlock( &cmdlock );

    }
    while ( control->status != mpc_quit );

    for( i=0; i <= control->fade; i++ ) {
    	kill( pid[i], SIGTERM );
    }

    addMessage( 2, "Reader stopped" );

    return NULL;
}
