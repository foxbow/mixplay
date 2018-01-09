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

int main( int argc, char **argv ) {
    mpconfig    *config;
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

        config=writeConfig( path );
        if( config == NULL ) {
        	fail( F_FAIL, "Could not create config file!" );
        }
    }

    setVerbosity(1);

    if( ( getArgs( argc, argv ) != 0 ) && ( config->remote ) ) {
		addMessage( 0, "Disabling remote connection" );
		config->remote=0;
	}

    initAll( 0 );

    /* Start curses mode */
    config->inUI=-1;
    initscr();
    curs_set( 0 );
    cbreak();
    keypad( stdscr, TRUE );
    noecho();
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
						if( 0 != config->current->key ) {
							popUp( 0, "%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s",
								   config->current->path, config->current->genre,
								   config->current->key, config->current->playcount,
								   config->current->skipcount,
								   ONOFF( ~( config->current->flags )&MP_CNTD ),
								   ONOFF( ~( config->current->flags )&MP_SKPD ) );
						}
						else {
							popUp( 2, config->current->path );
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
						setCommand( mpc_dnptitle );
						break;

					case 'D':
					case 'B':
						setCommand( mpc_dnpalbum );
						break;

					case 'f': /* toggles the favourite flag on a title */
						setCommand( mpc_favtitle );
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

    if( config->changed ) {
    	writeConfig( NULL );
    }
    freeConfig( );

    dbClose( db );

    return( 0 );
}
