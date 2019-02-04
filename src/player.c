/*
 * player.c
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
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

static pthread_mutex_t _pcmdlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _asynclock=PTHREAD_MUTEX_INITIALIZER;

/**
 * returns TRUE when no asynchronous operation is running but does not
 * lock async operations.
 */
static int asyncTest() {
	int ret=0;
	addMessage( 1, "Async test" );
	if( pthread_mutex_trylock( &_asynclock ) != EBUSY ) {
		pthread_mutex_unlock( &_asynclock );
		ret=1;
	}
	return ret;
}

static snd_mixer_t *_handle=NULL;
static snd_mixer_elem_t *_elem=NULL;

/**
 * disconnects from the mixer and frees all resources
 */
static void closeAudio( ) {
	if( _handle != NULL ) {
		snd_mixer_detach(_handle, "default");
		snd_mixer_close(_handle);
		snd_config_update_free_global();
		_handle=NULL;
	}
}

/**
 * tries to connect to the mixer
 */
static long openAudio( const char *channel ) {
	snd_mixer_selem_id_t *sid=NULL;
	if( channel == NULL || strlen( channel ) == 0 ) {
		addMessage( 0, "No audio channel set" );
		return -1;
	}

	snd_mixer_open(&_handle, 0);
	if( _handle == NULL ) {
		addMessage( 1, "No ALSA support" );
		return -1;
	}

	snd_mixer_attach(_handle, "default");
	snd_mixer_selem_register(_handle, NULL, NULL);
	snd_mixer_load(_handle);

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, channel);
	_elem = snd_mixer_find_selem(_handle, sid);
	/**
	 * for some reason this can't be free'd explicitly.. ALSA is weird!
	 * snd_mixer_selem_id_free(_sid);
	 */
	if( _elem == NULL) {
		addMessage( 0, "Can't find channel %s!", channel );
		closeAudio();
		return -1;
	}

	return 0;
}

/**
 * adjusts the master volume
 * if volume is 0 the current volume is returned without changing it
 * otherwise it's changed by 'volume'
 * if abs is 0 'volume' is regarded as a relative value
 * if ALSA does not work or the current card cannot be selected -1 is returned
 */
static long controlVolume( long volume, int absolute ) {
	long min, max;
	int mswitch=0;
	long retval = 0;
	char *channel;
	mpconfig *config;
	config=getConfig();
	channel=config->channel;

	if( config->volume == -1 ) {
		addMessage( 0, "Volume control is not supported!" );
		return -1;
	}

	if( _handle == NULL ) {
		if( openAudio( channel ) != 0 ) {
			config->volume=-1;
			return -1;
		}
	}

	/* if audio is muted, don't change a thing */
	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem, SND_MIXER_SCHN_FRONT_LEFT, &mswitch);
		if( mswitch == 0 ) {
			config->volume=-2;
			return -2;
		}
	}

	snd_mixer_selem_get_playback_volume_range(_elem, &min, &max);
	if( absolute != 0 ) {
		retval=volume;
	}
	else {
		snd_mixer_handle_events(_handle);
		snd_mixer_selem_get_playback_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT, &retval );
		retval=(( retval * 100 ) / max)+1;
		retval+=volume;
	}

	if( retval < 0 ) retval=0;
	if( retval > 100 ) retval=100;
	snd_mixer_selem_set_playback_volume_all(_elem, ( retval * max ) / 100);

	config->volume=retval;
	return retval;
}

/*
 * naming wrappers for controlVolume
 */
#define setVolume(v)	controlVolume(v,1)
#define getVolume()	 controlVolume(0,0)
#define adjustVolume(d) controlVolume(d,0)

/*
 * toggles the mute states
 * returns -1 if mute is not supported
 *         -2 if mute was enabled
 *         the current volume on unmute
 */
int toggleMute() {
	mpconfig *config=getConfig();
	int mswitch;

	if( config->volume == -1 ) {
		return -1;
	}
	if( _handle == NULL ) {
		if( openAudio(config->channel ) != 0 ) {
			config->volume=-1;
			return -1;
		}
	}
	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem, SND_MIXER_SCHN_FRONT_LEFT, &mswitch);
		if( mswitch == 1 ) {
			snd_mixer_selem_set_playback_switch_all(_elem, 0);
			config->volume=-2;
		}
		else {
			snd_mixer_selem_set_playback_switch_all(_elem, 1);
			config->volume=getVolume();
		}
	}
	else {
		return -1;
	}

	return config->volume;
}

/**
 * sets the given stream
 */
void setStream( const char* stream, const char *name ) {
	mpconfig *control=getConfig();
	control->current=wipePlaylist( control->current );
	control->root=wipeTitles(control->root);
	control->current=addPLDummy( control->current, "Playing stream" );
	control->current=addPLDummy( control->current, name );
	control->current=control->current->next;
	control->streamURL=(char *)frealloc( control->streamURL, strlen(stream)+1 );
	strcpy( control->streamURL, stream );
	addMessage( 1, "Play Stream %s (%s)", name, stream );
}

/**
 * sends a command to the player
 * also makes sure that commands are queued
 */
void setCommand( mpcmd cmd ) {
	if( cmd == mpc_idle ) {
		return;
	}

	pthread_mutex_lock( &_pcmdlock );
	getConfig()->command=cmd;
}


/**
 * make mpeg123 play the given title
 */
static void sendplay( int fdset, mpconfig *control ) {
	int res=0;
	char line[MAXPATHLEN+6]="load ";
	assert( control->current != NULL );

	if( !control->playstream ) {
		if( control->active > 0 ) {
			strtcat( line, control->musicdir, MAXPATHLEN+6 );
		}
		strtcat( line, control->current->title->path, MAXPATHLEN+6 );
		strtcat( line, "\n", MAXPATHLEN+6 );
	}
	else {
		strtcat( line, control->streamURL, MAXPATHLEN+6 );
		strtcat( line, "\n", MAXPATHLEN+6 );
	}

	res=write( fdset, line, MAXPATHLEN+6 );
	if ( (res == -1 ) || ( (unsigned)res < strlen( line ) ) ) {
		fail( errno, "Could not write\n%s", line );
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
	int64_t cactive;
	mpconfig *control=( mpconfig * )data ;
	char *home=getenv("HOME");

	if( home == NULL ) {
		fail( -1, "Cannot get homedir!" );
	}
	cactive=control->active;

	/* stream selected */
	if( cactive < 0 ) {
		active = -(cactive+1);
		control->playstream=1;

		if( active >= control->streams ) {
			addMessage( 0, "Stream #%i does no exist!", active );
			control->active=1;
			return setProfile(data);
		}

		setStream( control->stream[active], control->sname[active] );
	}
	/* profile selected */
	else if( cactive > 0 ){
		active=cactive-1;
		control->playstream=0;

		if( active > control->profiles ) {
			addMessage ( 0, "Profile #%i does no exist!", active );
			control->active=1;
			return setProfile(data);
		}

		profile=control->profile[active];

		snprintf( confdir, MAXPATHLEN, "%s/.mixplay", home );

		control->dnpname=(char*)falloc( MAXPATHLEN, 1 );
		snprintf( control->dnpname, MAXPATHLEN, "%s/%s.dnp", confdir, profile );

		control->favname=(char*)falloc( MAXPATHLEN, 1 );
		snprintf( control->favname, MAXPATHLEN, "%s/%s.fav", confdir, profile );
		dnplist=loadList( control->dnpname );
		favourites=loadList( control->favname );

		control->current=wipePlaylist( control->current );
		control->root=wipeTitles( control->root );
		control->root=dbGetMusic( control->dbname );

		if( NULL == control->root ) {
			addMessage( 0, "Scanning musicdir" );
			lastver=getVerbosity();

			if( lastver < 1 ) {
				setVerbosity( 1 );
			}

			num = dbAddTitles( control->dbname, control->musicdir );

			if( 0 == num ) {
				fail( F_FAIL, "No music found at %s!", control->musicdir );
			}

			addMessage( 0, "Added %i titles.", num );
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
		applyFAVlist( control->root, favourites );
		plCheck( 0 );
		cleanList( dnplist );
		cleanList( favourites );

		addMessage( 1, "Profile set to %s.", profile );
		if( control->argument != NULL ) {
			progressEnd();
			/* do not free, the string has become the new profile entry! */
			control->argument=NULL;
		}
	}

	/* if we're not in player context, start playing automatically */
	control->status=mpc_start;
	if( pthread_mutex_trylock( &_pcmdlock ) != EBUSY ) {
		addMessage( 1, "Start play" );
		control->command=mpc_start;
	}

	return NULL;
}

/**
 * checks if the playcount needs to be increased and if the skipcount
 * needs to be decreased. In both cases the updated information is written
 * back into the db.
 */
static void playCount( mptitle *title ) {
	if( getConfig()->playstream || ( title->key == 0 ) ) {
		return;
	}

	/* marked - default play, not marked - searchplay */
	if ( (title->flags&MP_MARK) && !(title->flags&MP_CNTD) ) {
		title->flags |= MP_CNTD; /* make sure a title is only counted once per session */
		title->playcount++;
		if( title->skipcount > 0 ) {
			title->skipcount--;
		}
		dbMarkDirty();
	}
}

/**
 * asnchronous functions to run in the background and allow updates being sent to the
 * client
 */
static void *plCheckDoublets( void *arg ) {
	mpconfig *control=(mpconfig *)arg;
	int i;

	progressStart( "Filesystem Cleanup" );

	/* update database with current playcount etc */
	if( control->dbDirty > 0 ) {
		dbWrite( );
	}
	addMessage( 0, "Checking for doubles.." );
	i=dbNameCheck( control->dbname );
	if( i > 0 ) {
		addMessage( 0, "Deleted %i titles", i );
		plCheck( 1 );
	}
	else {
		addMessage( 0, "No titles deleted" );
	}
	progressEnd( );
	pthread_mutex_unlock( &_asynclock );;
	return NULL;
}

static void *plDbClean( void *arg ) {
	mpconfig *control=(mpconfig *) arg;
	int i;
	progressStart( "Database Cleanup" );

	/* update database with current playcount etc */
	if( control->dbDirty ) {
		dbWrite( );
	}
	addMessage( 0, "Checking for deleted titles.." );
	i=dbCheckExist( control->dbname );

	if( i > 0 ) {
		addMessage( 0, "Removed %i titles", i );
		plCheck( 1 );
	}
	else {
		addMessage( 0, "No titles removed" );
	}

	addMessage( 0, "Checking for new titles.." );
	i=dbAddTitles( control->dbname, control->musicdir );

	if( i > 0 ) {
		addMessage( 0, "Added %i new titles", i );
	}
	else {
		addMessage( 0, "No titles to be added" );
	}

	progressEnd( );
	pthread_mutex_unlock( &_asynclock );;
	return NULL;
}

static void *plDbInfo( void *arg ) {
	mpconfig *control=(mpconfig *) arg;
	progressStart( "Database Info" );
	addMessage( 0, "Music dir: %s", control->musicdir );
	dumpInfo( control->root, control->skipdnp );
	progressEnd();
	pthread_mutex_unlock( &_asynclock );;
	return NULL;
}

static void *plSetProfile( void *arg ) {
	mpconfig *control=(mpconfig *) arg;
	control->status=mpc_start;
	if( control->dbname[0] == 0 ) {
		readConfig( );
	}
	/* update database with current playcount etc */
	if( control->dbDirty ) {
		dbWrite( );
	}
	setProfile( control );
	sfree( &(control->argument) );
	pthread_mutex_unlock( &_asynclock );;
	return NULL;
}

/**
 * run the given command asynchronously to allow updates during execution
 * if channel is != -1 then playing the song will be paused during execution
 */
static void asyncRun( void *cmd(void *), mpconfig *control ) {
	pthread_t pid;
	if( pthread_mutex_trylock( &_asynclock ) == EBUSY ) {
		addMessage( 0, "Sorry, still blocked!" );
	}
	else {
		if( pthread_create( &pid, NULL, cmd, (void*)control ) < 0) {
			addMessage( 0, "Could not create async thread!" );
		}
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
void *reader( void *data ) {
	mpconfig	*control=getConfig();
	mptitle		*title=NULL;
	fd_set			fds;
	struct timeval	to;
	struct timespec ts;
	int64_t	i, key;
	int		invol=80;
	int		outvol=80;
	int 	fdset=0;
	int		profile;
	char 	line[MAXPATHLEN];
	char 	*a, *t;
	int 	order=1;
	int 	intime=0;
	int		oldtime=0;
	int 	fade=3;
	int 	p_status[2][2];			/* status pipes to mpg123 */
	int 	p_command[2][2];		/* command pipes to mpg123 */
	pid_t	pid[2];
	mpcmd	cmd=mpc_idle;
	int		update=0;
	int		insert=0;

	void blockSigint();

	addMessage( 1, "Reader starting" );

	if( control->fade == 0 ) {
		addMessage( 1, "No crossfading" );
		fade=0;
	}

	ts.tv_nsec=250000;
	ts.tv_sec=0;

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

			/* this process needs no pipe handles */
			close( p_command[i][0] );
			close( p_command[i][1] );
			close( p_status[i][0] );
			close( p_status[i][1] );
			/* Start mpg123 in Remote mode */
			execlp( "mpg123", "mpg123", "-R", "--rva-mix", "--skip-id3v2", "2> &1", NULL );
			fail( errno, "Could not exec mpg123" );
		}

		close( p_command[i][0] );
		close( p_status[i][1] );
	}

	/* check if we can control the system's volume */
	control->volume=getVolume();
	if( control->volume != -1  ) {
		addMessage(  1, "Hardware volume level is %i%%", control->volume );
	}
	else {
		addMessage( 0, "No hardware volume control!" );
		control->channel=NULL;
	}

	/* main loop */
	do {
		FD_ZERO( &fds );
		for( i=0; i<=control->fade; i++ ) {
			FD_SET( p_status[i][0], &fds );
		}
		to.tv_sec=1;
		to.tv_usec=0; /* 1/10 second */
		i=select( FD_SETSIZE, &fds, NULL, NULL, &to );
		update=0;

		/* drain inactive player */
		if( control->fade && FD_ISSET( p_status[fdset?0:1][0], &fds ) ) {
			key=readline( line, MAXPATHLEN, p_status[fdset?0:1][0] );

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
						if( control->current == NULL ) {
							fail( F_FAIL, "ERROR: %s", line );
						}
						else {
							fail( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
								control->current->title->key,
								control->current->title->display,
								control->current->title->path );
						}
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
				( 3 < readline( line, MAXPATHLEN, p_status[fdset][0] ) ) ) {
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
					if( ( control->current != NULL ) &&( NULL != strstr( line, "ICY-" ) ) ) {
						if( NULL != strstr( line, "ICY-NAME: " ) ) {
							strip( control->current->title->album, line+13, NAMELEN );
							update=-1;
						}

						if( NULL != ( a = strstr( line, "StreamTitle" ) ) ) {
							addMessage( 3, "%s", a );
							a = a + 13;
							t=strchr( a, '\'' );
							if( t != NULL ) {
								*t = 0;
							}
							control->current=addPLDummy( control->current, a );
							/* carry over stream title as album entry */
							strtcpy( control->current->title->album, control->current->next->title->album, NAMELEN );
							/* if possible cut up title and artist */
							if( NULL != ( t = strstr( a, " - " ) ) ) {
								*t=0;
								t=t+3;
								strip( control->current->title->artist, a, NAMELEN );
								strip( control->current->title->title, t, NAMELEN );
							}
							plCheck(0);
							update=-1;
						}
					}
					break;

				case 'T': /* TAG reply */
					addMessage( 1, "Got TAG reply?!" );
					break;

				case 'J': /* JUMP reply */
					break;

				case 'S': /* Status message after loading a song (stream info) */
					break;

				case 'F': /* Status message during playing (frame info) */
					/* $1   = framecount (int)
					 * $2   = frames left this song (int)
					 * in  = seconds (float)
					 * rem = seconds left (float)
					 */
					a=strrchr( line, ' ' );
					if( a == NULL ) {
						addMessage( 0, "Error in Frame info: %s", line );
						break;
					}
					rem=strtof( a, NULL );
					*a=0;
					a=strrchr( line, ' ' );
					if( a == NULL ) {
						addMessage( 0, "Error in Frame info: %s", line );
						break;
					}
					intime=atoi( a );

					if( invol < 100 ) {
						invol++;
						snprintf( line, MAXPATHLEN, "volume %i\n", invol );
						write( p_command[fdset][1], line, strlen( line ) );
					}

					if( intime != oldtime ) {
						oldtime=intime;
						update=-1;
					}
					else {
						break;
					}

					if( intime/60 < 60 ) {
						sprintf( control->playtime, "%02i:%02i", intime/60, intime%60 );
					}
					else {
						sprintf( control->playtime, "%02i:%02i:%02i", intime/3600, ( intime%3600 )/60, intime%60 );
					}
					/* file play */
					if( (control->current != NULL ) && (control->playstream == 0 ) ) {
						control->percent=( 100*intime )/( rem+intime );
						sprintf( control->remtime, "%02i:%02i", (int)rem/60, (int)rem%60 );

						if( ( control->fade ) && ( rem <= fade ) ){
							/* should the playcount be increased? */
							playCount( control->current->title );

							if( control->current->next == control->current ) {
								control->status=mpc_idle; /* Single song: STOP */
							}
							else {
								control->current=control->current->next;
								plCheck( 0 );
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
					}
					break;

				case 'P': /* Player status */
					cmd = atoi( &line[3] );

					switch ( cmd ) {
					case 0: /* STOP */
						addMessage(2,"Player %i stopped", fdset );
						/* player was not yet fully initialized, start again */
						if( control->status == mpc_start ) {
							addMessage(2,"Restart player %i..", fdset );
							sendplay( p_command[fdset][1], control );
						}
						/* stream stopped playing (PAUSE) */
						else if( control->playstream ) {
							addMessage(2,"Stream stopped");
							control->status=mpc_idle;
						}
						/* we're playing a playlist */
						else if( control->current != NULL ){
							addMessage(2,"Title change on player %i", fdset );
							/* should the playcount be increased? */
							if( control->fade == 0 ) {
								playCount( control->current->title );
							}

							if( order < 0 ) {
								while( ( control->current->prev != NULL ) && order < 0 ) {
									control->current=control->current->prev;
									order++;
								}
								/* ignore skip before first title in playlist */
							}

							if( order > 0 ) {
								while( ( control->current->next != NULL ) && order > 0 ) {
									control->current=control->current->next;
									plCheck(0);
									order--;
								}
								/* stop on end of playlist */
								if( control->current->next == NULL ) {
									control->status=mpc_idle; /* stop */
								}
							}

							if( control->status != mpc_idle ) {
								sendplay( p_command[fdset][1], control );
							}

							order=1;
						}
						else {
							addMessage( 0, "Player status without current title" );
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
						addMessage( 0, "Unknown status %i on player %i!\n%s", cmd, fdset, line );
						break;
					}

					update=-1;
					break;

				case 'V': /* volume reply */
					break;

				case 'E':
					addMessage( 0, "Player %i: %s!", fdset, line+3 );
					if( control->current != NULL ) {
						addMessage( 1, "Index: %i\nName: %s\nPath: %s",
								control->current->title->key,
								control->current->title->display,
								control->current->title->path );
					}
					control->status=mpc_idle;
					break;

				default:
					addMessage( 0, "Player %i: Warning!\n%s", fdset, line );
					break;
				} /* case line[1] */
			} /* if line starts with '@' */
			else {
				/* verbosity 1 as sometimes tags appear here which confuses on level 0 */
				addMessage( 1, "Player %i - MPG123: %s", fdset, line );
			}
		} /* fgets() > 0 */

		/*
		 * check if a command is set and read that command until it is not mpc_idle
		 * this is a very unlikely race condition that _pcmdlock is locked and
		 * the command is not yet set but it may happen and we want to make sure no
		 * command is dropped.
		 */
		if ( pthread_mutex_trylock( &_pcmdlock ) == EBUSY ) {
			while( control->command == mpc_idle ) {
				addMessage( 1, "Idling on command read" );
			}
			addMessage( 1, "MPC %s / %04x", mpcString(control->command), control->command );
		}
		else {
			control->command=mpc_idle;
		}
		cmd=MPC_CMD(control->command);

		/* get the target title for fav and dnp commands */
		if( control->current != NULL ) {
			title=control->current->title;
			if( ( cmd == mpc_fav ) || ( cmd == mpc_dnp ) ) {
				if( control->argument != NULL ) {
					if( ( title=getTitleByIndex(atoi( control->argument ) ) ) == NULL ) {
						title=control->current->title;
					}
					sfree( &(control->argument) );
				}
			}
		}

		switch( cmd ) {
		case mpc_start:
			plCheck( 0 );
			if( control->status == mpc_start ) {
				write( p_command[fdset][1], "STOP\n", 6 );
			}
			else {
				control->status=mpc_start;
				sendplay( p_command[fdset][1], control );
			}
			break;

		case mpc_play:
			/* has the player been properly initialized yet? */
			if( control->status != mpc_start ) {
				/* streamplay cannot be properly paused for longer */
				if( control->playstream ) {
					if( control->status == mpc_play ) {
						write( p_command[fdset][1], "STOP\n", 6 );
					}
					/* restart the stream in case it broke */
					else {
						sendplay( p_command[fdset][1], control );
					}
				}
				/* just toggle pause on normal play */
				else {
					write( p_command[fdset][1], "PAUSE\n", 7 );
					control->status=( mpc_play == control->status )?mpc_idle:mpc_play;
				}
			}
			/* initialize player after startup */
			else {
				plCheck( 0 );
				if( control->current != NULL ) {
					addMessage( 1, "Autoplay.." );
					sendplay( p_command[fdset][1], control );
				}
			}
			break;

		case mpc_prev:
			if( asyncTest() ) {
				order=-1;
				if( control->argument != NULL ) {
					order=-atoi(control->argument);
					sfree(&(control->argument));
				}
				write( p_command[fdset][1], "STOP\n", 6 );
			}
			break;

		case mpc_next:
			if( ( control->current != NULL ) && asyncTest() ) {
				order=1;
				if( control->argument != NULL ) {
					order=atoi(control->argument);
					sfree(&(control->argument));
				}

				if( ( control->current->title->key != 0 ) &&
					!( control->current->title->flags & ( MP_CNTD ) ) &&
					!( control->current->title->flags & ( MP_MARK ) ) ) {
					control->current->title->skipcount++;
					/* updateCurrent( control ); - done in STOP handling */
				}

				write( p_command[fdset][1], "STOP\n", 6 );
			}
			break;

		case mpc_doublets:
			asyncRun( plCheckDoublets, control );
			break;

		case mpc_dbclean:
			asyncRun( plDbClean, control );
			break;

		case mpc_stop:
			if( asyncTest() ) {
				order=0;
				write( p_command[fdset][1], "STOP\n", 6 );
			}
			break;

		case mpc_dnp:
			if( (title != NULL ) && asyncTest() ) {
				handleRangeCmd( title, control->command );
				plCheck( 0 );
				order=0;
				write( p_command[fdset][1], "STOP\n", 6 );
			}
			break;

		case mpc_fav:
			if( (title != NULL ) && asyncTest() ) {
				handleRangeCmd( title, control->command );
			}
			break;

		case mpc_repl:
			write( p_command[fdset][1], "JUMP 0\n", 8 );
			break;

		case mpc_QUIT:
		case mpc_quit:
			/* The player does not know about the main App so anything setting mcp_quit
			 * MUST make sure that the main app terminates as well ! */
			if( asyncTest() ) {
				control->status=mpc_quit;
			}
			break;

		case mpc_profile:
			if( asyncTest() ) {
				if( control->argument == NULL ) {
					progressMsg( "No profile given!" );
				}
				else {
					profile=atoi( control->argument );
					if( ( profile != 0 ) && ( profile != control->active ) ) {
						control->active=profile;
						control->changed = 1;
						asyncRun( plSetProfile, control );
					}
				}
			}
			break;

		case mpc_newprof:
			if ( ( control->current != NULL ) && asyncTest() ) {
				if( control->argument == NULL ) {
					progressMsg( "No profile given!" );
				}
				else if(control->playstream){
					control->streams++;
					control->stream=(char**)frealloc(control->stream, control->streams*sizeof( char * ) );
					control->stream[control->streams-1]=(char*)falloc( strlen(control->streamURL)+1, 1 );
					strcpy( control->stream[control->streams-1], control->streamURL );
					control->sname=(char**)frealloc(control->sname, control->streams*sizeof( char * ) );
					control->sname[control->streams-1]=control->argument;
					control->active=-(control->streams);
					writeConfig( NULL );
					control->argument=NULL;
				}
				else {
					control->profiles++;
					control->profile=(char**)frealloc( control->profile, control->profiles*sizeof(char*) );
					control->profile[control->profiles-1]=control->argument;
					control->active=control->profiles;
					writeConfig( NULL );
					control->argument=NULL;

					write( p_command[fdset][1], "STOP\n", 6 );
					control->status=mpc_start;
					if( control->dbname[0] == 0 ) {
						readConfig( );
					}
					setProfile( control );
					control->current->title = control->root;
					sendplay( p_command[fdset][1], control );
				}
			}
			break;

		case mpc_remprof:
			if( asyncTest() ) {
				if( control->argument == NULL ) {
					progressMsg( "No profile given!" );
				}
				else {
					profile=atoi( control->argument );
					if( profile == 0 ) {
						progressMsg( "Cannot remove empty profile!" );
					}
					if( profile > 0 ) {
						if( profile == control->active ) {
							progressMsg( "Cannot remove active profile!" );
						}
						else if( profile > control->profiles ) {
							progressStart( "Profile #%i does not exist!", profile );
							progressEnd();
						}
						else {
							free( control->profile[profile-1] );
							for(i=profile;i<control->profiles;i++) {
								control->profile[i-1]=control->profile[i];
							}
							control->profiles--;

							if( control->active > profile ) {
								control->active--;
							}
							writeConfig(NULL);
						}
					}
					else {
						profile=(-profile);
						if( profile > control->streams ) {
							progressStart( "Stream #%i does not exist!", profile );
							progressEnd();
						}
						else {
							free( control->stream[profile-1] );
							free( control->sname[profile-1] );
							for(i=profile;i<control->streams;i++) {
								control->stream[i-1]=control->stream[i];
								control->sname[i-1]=control->sname[i];
							}
							control->streams--;
							control->profiles--;
							if( profile > control->active ) {
								control->active=1;
							}
							else if( control->active < -profile ) {
								control->active=1;
							}
							writeConfig(NULL);
						}
					}
				}
			}

			break;

		case mpc_path:
			if( ( control->current != NULL ) && asyncTest() ) {
				if( control->argument == NULL ) {
					progressMsg( "No path given!" );
				}
				else {
					if( MPC_ISSHUFFLE(control->command) ) {
						control->plmix=1;
					}
					else {
						control->plmix=0;
					}
					if( setArgument( control->argument ) ){
						control->active = 0;
						if( control->status == mpc_start ) {
							write( p_command[fdset][1], "STOP\n", 6 );
						}
						else {
							control->status=mpc_start;
							sendplay( p_command[fdset][1], control );
						}
					}
					sfree( &(control->argument) );
				}
			}
			break;

		case mpc_ivol:
			adjustVolume( +VOLSTEP );
			update=-1;
			break;

		case mpc_dvol:
			adjustVolume( -VOLSTEP );
			update=-1;
			break;

		case mpc_bskip:
			write( p_command[fdset][1], "JUMP -64\n", 10 );
			break;

		case mpc_fskip:
			write( p_command[fdset][1], "JUMP +64\n", 10 );
			break;

		case mpc_dbinfo:
			asyncRun( plDbInfo, control );
			break;

		case mpc_search:
			if( asyncTest() ) {
				if( control->argument == NULL ) {
					progressStart( "Nothing to search for!" );
					progressEnd();
				}
				else {
					if ( search( control->argument, MPC_RANGE(control->command), 0 ) == -1 ) {
						addMessage( 0, "Too many titles found!" );
					}
					while( control->found->send == -1 ) {
						nanosleep( &ts, NULL );
					}
					progressEnd();
					sfree( &(control->argument) );
				}
			}
			break;

		case mpc_insert:
			insert=-1;
			/* no break */

		case mpc_append:
			if( control->argument == NULL ) {
				addMessage( 0, "No play info set!" );
			}
			else {
				playResults( MPC_RANGE( control->command ), control->argument, insert );
				sfree( &(control->argument) );
			}
			insert=0;
			break;

		case mpc_setvol:
			if( control->argument != NULL ) {
				setVolume( atoi(control->argument) );
				sfree( &(control->argument) );
			}
			update=-1;
			break;

		case mpc_edit:
			control->pledit=!control->pledit;
			break;

		case mpc_wipe:
			if( control->pledit==1 ) {
				control->current=wipePlaylist(control->current);
				order=0;
				write( p_command[fdset][1], "STOP\n", 6 );
			}
			else {
				addMessage( 0, "Got wipe without active edit!" );
			}
			break;

		case mpc_save:
			if( ( control->pledit==1 ) && ( control->argument != NULL ) ) {
				writePlaylist( control->current, control->argument );
				sfree( &(control->argument) );
				updatePlaylists();
			}
			else {
				addMessage( 0, "Got save without active edit!" );
			}
			break;

		case mpc_remove:
			if( ( control->pledit==1 ) && ( control->argument != NULL ) ) {
				control->current=remFromPLByKey( control->current, atoi( control->argument ) );
				sfree( &(control->argument) );
			}
			else {
				addMessage( 0, "Got remove without active edit!" );
			}

			break;

		case mpc_mute:
			toggleMute();
			break;

		case mpc_idle:
			/* read current Hardware volume in case it changed externally
			 * don't read before control->argument is NULL as someone may be
			 * trying to set the volume right now */
			if( (control->argument == NULL) && ( control->volume != -1 ) ) {
				control->volume=getVolume( );
			}
			break;

		default:
			addMessage( 0, "Received illegal command %i", cmd );
			/* modifiers are ignored */
			break;
		}

		control->command=mpc_idle;
		pthread_mutex_unlock( &_pcmdlock );

		/* notify UI that something has changed */
		if( update ) {
			updateUI( );
		}
	}
	while ( control->status != mpc_quit );

	closeAudio();
	/* stop player(s) gracefully */
	if( control->dbDirty > 0 ) {
		addMessage( 0, "Updating Database" );
		dbWrite( );
	}

	sleep(1);
	addMessage( 0, "Player stopped" );

	for( i=0; i<control->fade; i++) {
		write( p_command[fdset][i], "QUIT\n", 6 );
	}

	return NULL;
}
