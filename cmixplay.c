#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <X11/Xlib.h>
#include <getopt.h>
#include <ncurses.h>

#include "utils.h"
#include "ncbox.h"
#include "musicmgr.h"
#include "database.h"
#include "player.h"
#include "config.h"

/* global control structure */
mpconfig *mpcontrol;

int main( int argc, char **argv ) {
    unsigned char	c;
    mpconfig    control;
    int 		db=0;
    fd_set fds;
    struct timeval to;
/*    char line[MAXPATHLEN]; */
    long key;
    char status[MAXPATHLEN] = "INIT";

    muteVerbosity();
    mpcontrol=&control;

    memset( mpcontrol, 0, sizeof( struct mpcontrol_t ) );

    control.fade=1;

    /* parse command line options */
    /* using unsigned char c to work around getopt bug on ARM */
    while ( ( c = getopt( argc, argv, "vF" ) ) != 255 ) {
        switch ( c ) {
        case 'v': /* increase debug message level to display in console output */
            incVerbosity();
            break;

        case 'F': /* single channel - disable fading */
        	control.fade=0;
        	break;
        }
    }

    /* check for existance ? */
    readConfig( &control );

    if ( optind < argc ) {
    	if( 0 == setArgument( &control, argv[optind] ) ) {
            fail( F_FAIL, "Unknown argument!\n", argv[optind] );
            return -1;
        }
    }

    /* start the actual player */
    pthread_create( &control.rtid, NULL, reader, (void *)&control );

    if( control.root == NULL ) {
    	setProfile( &control );
    }
    else {
    	control.active=0;
        control.dbname[0]=0;
        setCommand(&control, mpc_play );
    }

    /* Start curses mode */
    initscr();
    curs_set( 0 );
    cbreak();
    keypad( stdscr, TRUE );
    noecho();
    drawframe( NULL, status, control.playstream );

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
					setCommand(&control, mpc_play );
					break;

				case 's':
					setCommand(&control, mpc_stop );
					break;

				case 'q':
					setCommand( &control, mpc_quit );
					break;
				}

				if( ! control.playstream ) {
					switch( key ) {
					case '+':
						setCommand(&control, mpc_ivol );
						break;

					case '-':
						setCommand( &control, mpc_dvol );
						break;

					case 'i':
						if( 0 != control.current->key ) {
							popUp( 0, "%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s",
								   control.current->path, control.current->genre,
								   control.current->key, control.current->playcount,
								   control.current->skipcount,
								   ONOFF( ~( control.current->flags )&MP_CNTD ),
								   ONOFF( ~( control.current->flags )&MP_SKPD ) );
						}
						else {
							popUp( 2, control.current->path );
						}

						break;
#if 0
					case 'S':
					case 'J':
						popAsk( "Search: ", line );
						if( strlen( line ) > 2 ) {
							next=control.current;

							do {
								if( checkMatch( next->path, line ) ) {
									break;
								}

								next=next->plnext; /* @TODO: include DNPs? */
							}
							while( control.current != next );

							if( next != control.current ) {
								next->flags|=MP_CNTD; /* Don't count searched titles. */
								moveEntry( next, control.current );
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
/*
					case 'F':
						fade=fade?0:3;
						popUp( 1, "Fading is now %s", ONOFF( fade ) );
						break;

					case 'R':
						repeat=repeat?0:1;
						popUp( 1, "Repeat is now %s", ONOFF( repeat ) );
						break;
*/
					case 'I':
						popUp( 0, "   fade: %s\n"
							   "dnplist: %s\n"
							   "favlist: %s\n"
							   , ONOFF( control.fade ), control.dnpname, control.favname );
						break;

					case KEY_DOWN:
					case 'n':
						setCommand(&control, mpc_next );
						break;

					case KEY_UP:
					case 'p':
						setCommand(&control, mpc_prev );
						break;

/*
					case 'N':
						order=5;
						write( p_command[fdset][1], "STOP\n", 6 );
						break;

					case 'P':
						order=-5;
						write( p_command[fdset][1], "STOP\n", 6 );
						break;

					case KEY_LEFT:
						write( p_command[fdset][1], "JUMP -64\n", 10 );
						break;

					case KEY_RIGHT:
						write( p_command[fdset][1], "JUMP +64\n", 10 );
						break;
*/
					case 'r':
						setCommand(&control, mpc_repl );
						break;

					case 'b':
						setCommand(&control, mpc_dnptitle );
						break;

					case 'B':
						setCommand(&control, mpc_dnpalbum );
						break;

					case 'f': /* toggles the favourite flag on a title */
						setCommand(&control, mpc_favtitle );
						break;
					}
				}
			}
		}
        if( control.playstream ) {
            sprintf( status, "%s PLAYING", control.playtime );
        }
        else {
            sprintf( status, "%s PLAYING %s", control.playtime, control.remtime );
        }

		drawframe( control.current, status, control.playstream );

    } while( control.status != mpc_quit );
    endwin();

    printver( 2, "Dropped out of the main loop\n" );

    pthread_join( control.rtid, NULL );

    freeConfig( &control );

    dbClose( db );

    return( 0 );
}
