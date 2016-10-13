/**
 * collection of ncurses helper functions
 */

#include "ncbox.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>

/**
 * draws a pop-up into the bottom of the current frame
 * supports multiple lines divided by '\n' characters
 * trailing '\n's are ignored
 * if a line is too long, the beginning of the line is cut
 * until the rest fits.
 */
void popUp( const char *text ) {
	int row, col, line;
	int numlines=1;
	char buff[1024];
	char *p;
	char **lines;

	strncpy( buff, text, 1024 );
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
	}
	refresh();
	free(lines);
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
