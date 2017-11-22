#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <getopt.h>
#include <ncurses.h>

#include "utils.h"
#include "ncbox.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "config.h"

int main( int argc, char **argv ) {
    unsigned char	c;
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
    config->fade=1;

    /* parse command line options */
    /* using unsigned char c to work around getopt bug on ARM */
    while ( ( c = getopt( argc, argv, "dvfF" ) ) != 255 ) {
        switch ( c ) {
        case 'v': /* increase debug message level to display in console output */
            incVerbosity();
            break;

        case 'd':
        	incDebug();
        	break;

        case 'f':
        	config->fade=0;
        	break;

        case 'F': /* toggle fading */
        	config->fade=1;
        	break;
        }
    }

    if ( optind < argc ) {
    	if( 0 == setArgument( argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    /* start the actual player */
    pthread_create( &config->rtid, NULL, reader, (void *)config );

    if( config->root == NULL ) {
    	setProfile( config );
    }
    else {
    	config->active=0;
        config->dbname[0]=0;
        setCommand( mpc_play );
    }

    /* Start curses mode */
    config->inUI=-1;
    initscr();
    curs_set( 0 );
    cbreak();
    keypad( stdscr, TRUE );
    noecho();
    drawframe( NULL, "INIT", config->playstream );

    /* The main control loop */
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

				case 'q':
					setCommand( mpc_quit );
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
						popUp( 0, "   fade: %s\n"
							   "dnplist: %s\n"
							   "favlist: %s\n"
							   , ONOFF( config->fade ), config->dnpname, config->favname );
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

    freeConfig( );

    dbClose( db );

    return( 0 );
}
