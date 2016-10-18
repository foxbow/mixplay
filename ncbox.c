/**
 * collection of ncurses helper functions
 */

#include "ncbox.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int _ncboxpopup = 0;

/**
 * check if a popUp is currently active
 */
int popUpActive() {
	return _ncboxpopup;
}

/**
 * close a popUp
 */
void popDown() {
	_ncboxpopup=0;
}

/**
 * draws a pop-up into the bottom of the current frame
 * supports multiple lines divided by '\n' characters
 * trailing '\n's are ignored
 * The format is line *printf()
 * if a line is too long, the beginning of the line is cut
 * until the rest fits.
 * the function will sleep for time seconds afterwards
 * if time=0 it will wait for a keypress
 */
void popUp( int time, const char *text, ... ) {
	int row, col, line;
	int numlines=1;
	char buff[1024];
	char *p;
	char **lines;
	va_list vargs;

	va_start( vargs, text );
	vsnprintf( buff, 1024, text, vargs );
	va_end( vargs );

	p=buff;

	while( '\n' == buff[ strlen(buff) ] ) buff[ strlen(buff) ]=0;

	while( NULL != strchr(p,'\n') ) {
		numlines++;
		p=strchr(p,'\n')+1;
	}

	lines=calloc( numlines, sizeof( char *) );

	lines[0]=buff;

	for( line=1; line < numlines; line++ ) {
		p=strchr( lines[line-1], '\n' );
		lines[line]=p+1;
		*p=0;
	}

	refresh();
	getmaxyx(stdscr, row, col);
	if ((row > (6+numlines) ) && (col > 19)) {
		mvhline( row-(numlines+3), 2, '=', col-4);

		for( line=0; line < numlines; line++ ) {

			if( strlen( lines[line] ) > col-6 ) {
				strcpy( buff, ( lines[line] )+(strlen( lines[line] )-(col-7)) );
			}
			else {
				strcpy( buff, lines[line] );
			}

			mvhline( row-numlines+line-2, 2, ' ', col-4);
			mvprintw( row-numlines+line-2, 3, " %s ", buff);
		}

		mvhline( row-2, 2, '=', col-4);
	}
	refresh();
	free(lines);

	if( 0 == time ) {
		_ncboxpopup=-1;
	}
	else {
		sleep(time);
	}
}

/**
 * ask for a string in a popUp()
 * this one is modal and will not return enter has been pressed
 */
void popAsk( const char *text, char *reply ) {
	int row, col;

	refresh();
	getmaxyx(stdscr, row, col);
	if ((row > 6 ) && (col > 19)) {
		mvhline( row-4, 2, '=', col-4);
		mvhline( row-3, 2, ' ', col-4);
		mvhline( row-2, 2, '=', col-4);
		mvprintw( row-3, 3, " %s ", text );
		echo();
		getstr( reply );
		noecho();
	}
	refresh();
}

/**
 * Draw a horizontal line
 */
void dhline(int r, int c, int len) {
	mvhline(r, c + 1, HOR, len - 1);
	mvprintw(r, c, EDG );
	mvprintw(r, c + len, EDG );
}

/**
 * Draw a vertical line
 */
void dvline(int r, int c, int len) {
	mvhline(r + 1, c, VER, len - 2);
	mvprintw(r, c, EDG );
	mvprintw(r + len, c, EDG );
}

/**
 * draw a box
 */
void drawbox(int r0, int c0, int r1, int c1) {
	dhline(r0, c0, c1 - c0);
	dhline(r1, c0, c1 - c0);
	mvvline(r0 + 1, c0, VER, r1 - r0 - 1);
	mvvline(r0 + 1, c1, VER, r1 - r0 - 1);
}
