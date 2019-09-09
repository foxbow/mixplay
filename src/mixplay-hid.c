/* HID client for mixplayd.
   proof of concept for the client architecture and preparation for the
	 flirc interface. */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <ncurses.h>

#include "json.h"
#include "config.h"
#include "mpclient.h"
#include "utils.h"

/*
 * Print errormessage and exit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail( const int error, const char* msg, ... ) {
	va_list args;
	fprintf( stdout, "\n" );
	printf("mixplay-hid: ");
	va_start( args, msg );
	vfprintf( stdout, msg, args );
	va_end( args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
	}
	exit( error );
}

static void drawAll() {
	char title[80]="";
	if( getCurrentTitle( title, 79 ) < 0 ) {
		fail(errno, "Command failed.");
	}
	mvprintw( 1, 2, "Mixplay HID demo");
	mvprintw( 3, 2, "                                                                            ");
	mvprintw( 3, 2, "%s", title );
	refresh();
}

int main( ){
	int fd;
	char c=0;
	mpcmd_t cmd=mpc_idle;
	fd_set fds;
	struct timeval to;

	if( readConfig() == NULL ) {
		fail( F_FAIL, "No mixplayd configuration found!");
	}

	initscr();
	curs_set( 0 );
	/* disable buffering */
	cbreak();
	/* turn on ctrl-key shortcuts */
	keypad( stdscr, TRUE );
	noecho();

	fd=getConnection();
	while ( fd != -1 ) {
		FD_ZERO( &fds );
		FD_SET( fileno( stdin ), &fds );

		to.tv_sec=1;
		to.tv_usec=0;
		select( FD_SETSIZE, &fds, NULL, NULL, &to );
		/* Interpret key */
		if( FD_ISSET( fileno( stdin ), &fds ) ) {
			c=getch();
			switch(c) {
				case ' ':
					cmd=mpc_play;
					break;
				case 'n':
				case 's':
					cmd=mpc_next;
					break;
				case 'p':
					cmd=mpc_prev;
					break;
				case 'd':
					cmd=mpc_dnp;
					break;
				case 'f':
					cmd=mpc_fav;
					break;
				case 'q':
					close(fd);
					cmd=mpc_idle;
					fd=-1;
					break;
				default:
					cmd=mpc_idle;
			}
			if( sendCMD( fd, cmd ) < 0 ) {
				fail(errno, "Command %s failed.", mpcString(cmd) );
			}
			if( cmd != mpc_idle ) {
				mvprintw( 5, 2, "Sent %s", mpcString(cmd) );
			}
			else {
				mvprintw( 5, 2, "                      " );
			}
		}
		else {
			mvprintw( 5, 2, "                      " );
		}
		drawAll();
	}
	endwin();
}
