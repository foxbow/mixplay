#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <ncurses.h>

#include "utils.h"
#include "ncbox.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "config.h"
#include "mpcomm.h"

static int _ftrpos=0;

/**
 * show activity roller on console
 * this will only show when the global verbosity is larger than 0
 * spins faster with increased verbosity
 */
void activity( const char *msg, ... ) {
	char roller[5]="|/-\\";
	char text[256]="";
	int pos;
	va_list args;

	if( getVerbosity() && ( _ftrpos%( 100/getVerbosity() ) == 0 ) ) {
		pos=( _ftrpos/( 100/getVerbosity() ) )%4;

		va_start( args, msg );
		vsprintf( text, msg, args );
		printf( "%s %c		  \r", text, roller[pos] );
		fflush( stdout );
		va_end( args );
	}

	if( getVerbosity() > 0 ) {
		_ftrpos=( _ftrpos+1 )%( 400/getVerbosity() );
	}
}

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail( const int error, const char* msg, ... ) {
	va_list args;
	char line[512];

	va_start( args, msg );
	vsprintf( line, msg, args );
	va_end( args );

	if( error == 0 ) {
		addMessage( 0, "OBSOLETE: %s", line );
		return;
	}

	getConfig()->status=mpc_quit;

	fprintf( stderr, "\n" );

	if( error == F_FAIL ) {
		fprintf( stderr, "ERROR: %s\n", line );
	}
	else {
		fprintf( stderr, "ERROR: %s\n%i - %s\n", line, abs( error ), strerror( abs( error ) ) );
	}
	fprintf( stderr, "Press [ENTER]\n" );
	fflush( stdout );
	fflush( stderr );

	while( getc( stdin )!=10 );

	exit( error );

	return;
}

int main( int argc, char **argv ) {
	mpconfig	*config;
	int 		db=0;
	fd_set fds;
	struct timeval to;
	char path[MAXPATHLEN];
	long key;

	config=readConfig();

	if( config == NULL ) {
		printf( "music directory needs to be set.\n" );
		printf( "It will be set up now\n" );

		while( 1 ) {
			printf( "Default music directory:" );
			fflush( stdout );
			memset( path, 0, MAXPATHLEN );
			fgets(path, MAXPATHLEN, stdin );
			path[strlen( path )-1]=0; /* cut off CR */
			abspath( path, getenv( "HOME" ), MAXPATHLEN );

			if( isDir( path ) ) {
				break;
			}
			else {
				printf( "%s is not a directory!\n", path );
			}
		}

		writeConfig( path );
		config=readConfig();
		if( config == NULL ) {
			printf( "Could not create config file!\n" );
			return 1;
		}
	}

	setVerbosity(1);

	if( ( getArgs( argc, argv ) != 0 ) && ( config->remote == 1 ) ) {
		addMessage( 0, "Disabling remote connection" );
		config->remote=0;
	}

	initAll( );

	/* Start curses mode */
	initscr();
	curs_set( 0 );
	cbreak();
	keypad( stdscr, TRUE );
	noecho();
	config->inUI=-1;
	addUpdateHook( &nc_updateHook );
	drawframe( NULL, "INIT", config->playstream );

	do {
		FD_ZERO( &fds );
		FD_SET( fileno( stdin ), &fds );

		to.tv_sec=0;
		to.tv_usec=100000; /* 1/2 second */
		select( FD_SETSIZE, &fds, NULL, NULL, &to );

		/* Interpret key */
		if( FD_ISSET( fileno( stdin ), &fds ) ) {
			key=getch();

			if( popUpActive() ) {
				popDown();
			}
			else {
				switch( key ) {
				case ' ':
					setCommand( mpc_play );
					break;

				case 's':
					setCommand( mpc_stop );
					break;

				case 'Q':
					setCommand( mpc_QUIT );
					config->status=mpc_quit;
					break;

				case 'q':
					setCommand( mpc_quit );
					config->status=mpc_quit;
					break;
				}

				if( ! config->playstream ) {
					switch( key ) {
					case '+':
						setCommand( mpc_ivol );
						break;

					case '-':
						setCommand( mpc_dvol );
						break;

					case 'i':
						if( 0 != config->current->title->key ) {
							popUp( 0, "%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s",
								   config->current->title->path, config->current->title->genre,
								   config->current->title->key, config->current->title->playcount,
								   config->current->title->skipcount,
								   ONOFF( ~( config->current->title->flags )&MP_CNTD ),
								   ONOFF( ~( config->current->title->flags )&MP_SKPD ) );
						}
						else {
							popUp( 2, config->current->title->path );
						}

						break;
#if 0 /* todo: implement search properly */
					case 'S':
					case 'J':
						popAsk( "Search: ", line );
						if( strlen( line ) > 2 ) {
							next=config->current;

							do {
								if( checkMatch( next->path, line ) ) {
									break;
								}

								next=next->plnext; /* include DNPs? */
							}
							while( config->current != next );

							if( next != config->current ) {
								next->flags|=MP_CNTD; /* Don't count searched titles. */
								moveEntry( next, config->current );
								order=1;
								write( p_command[fdset][1], "STOP\n", 6 );
							}
							else {
								popUp( 2, "Nothing found for %s", line );
							}
						}
						else {
							popUp( 2, "Need at least 3 characters.." );
						}

						break;
#endif

					case 'I':
						popUp( 0,
							   "profile: %s\n"
							   "   fade: %s\n"
							   "dnplist: %s\n"
							   "favlist: %s",
							   config->profile[config->active-1], ONOFF( config->fade ), config->dnpname, config->favname );
						break;

					case KEY_DOWN:
					case 'n':
						setCommand( mpc_next );
						break;

					case KEY_UP:
					case 'p':
						setCommand( mpc_prev );
						break;

					case KEY_LEFT:
						setCommand( mpc_bskip );
						break;

					case KEY_RIGHT:
						setCommand( mpc_fskip );
						break;

					case 'r':
						setCommand( mpc_repl );
						break;

					case 'd':
					case 'b':
						if( config->argument != NULL ) {
							addMessage( 0, "Can't mark as DNP, argument is already set! [%s]", config->argument );
						}
						else {
							config->argument=calloc( sizeof(char), 10);
							snprintf( config->argument, 9, "%i", config->current->title->key );
							setCommand( mpc_dnp|mpc_title );
						}
						break;

					case 'D':
					case 'B':
						if( config->argument != NULL ) {
							addMessage( 0, "Can't mark as DNP, argument is already set! [%s]", config->argument );
						}
						else {
							config->argument=calloc( sizeof(char), 10);
							snprintf( config->argument, 9, "%i", config->current->title->key );
							setCommand( mpc_dnp|mpc_album );
						}
						break;

					case 'f': /* sets the favourite flag to a title */
						if( config->argument != NULL ) {
							addMessage( 0, "Can't mark as DNP, argument is already set! [%s]", config->argument );
						}
						else {
							config->argument=calloc( sizeof(char), 10);
							snprintf( config->argument, 9, "%i", config->current->title->key );
							setCommand( mpc_fav|mpc_title );
						}
						break;
					}
				}

			}
		}

	} while( config->status != mpc_quit );

	endwin();
	config->inUI=0;

	addMessage( 2, "Dropped out of the main loop" );

	pthread_join( config->rtid, NULL );
	pthread_join( config->stid, NULL );

	if( config->changed ) {
		writeConfig( NULL );
	}
	freeConfig( );

	dbClose( db );

	return( 0 );
}
