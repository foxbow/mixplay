/**
 * collection of ncurses helper functions
 */

#include "ncbox.h"
#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int _ncboxpopup = 0;
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
        printf( "%s %c          \r", text, roller[pos] );
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
 *         F_FAIL = print message w/o errno and exit
 */
void fail( int error, const char* msg, ... ) {
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
    int row, col, line, middle;
    int numlines=1;
    char buff[1024];
    char *p;
    char **lines;
    va_list vargs;

    va_start( vargs, text );
    vsnprintf( buff, 1024, text, vargs );
    va_end( vargs );

    p=buff;

    while( '\n' == buff[ strlen( buff ) ] ) {
        buff[ strlen( buff ) ]=0;
    }

    while( NULL != strchr( p,'\n' ) ) {
        numlines++;
        p=strchr( p,'\n' )+1;
    }

    lines=falloc( numlines, sizeof( char * ) );

    lines[0]=buff;

    for( line=1; line < numlines; line++ ) {
        p=strchr( lines[line-1], '\n' );
        lines[line]=p+1;
        *p=0;
    }

    refresh();
    getmaxyx( stdscr, row, col );

    if ( ( row > ( 2*numlines )+2 ) && ( col > 19 ) ) {
        middle=( ( row-1 )/2 )+1;
/*		mvhline( row-(numlines+3), 2, '=', col-4); */
        mvhline( middle, 2, '=', col-4 );
        middle++;

        for( line=0; line < numlines; line++ ) {

            if( strlen( lines[line] ) > col-6 ) {
                strcpy( buff, ( lines[line] )+( strlen( lines[line] )-( col-7 ) ) );
            }
            else {
                strcpy( buff, lines[line] );
            }

/*			mvhline( row-numlines+line-2, 2, ' ', col-4); */
/*			mvprintw( row-numlines+line-2, 3, " %s ", buff); */
            mvhline( middle+line, 2, ' ', col-4 );
            mvprintw( middle+line, 3, " %s ", buff );
        }

        mvhline( middle+line, 2, '=', col-4 );
    }

    refresh();
    free( lines );

    if( 0 == time ) {
        _ncboxpopup=-1;
    }
    else {
        sleep( time );
    }
}

/**
 * ask for a string in a popUp()
 * this one is modal and will not return enter has been pressed
 */
void popAsk( const char *text, char *reply ) {
    int row, col;

    refresh();
    getmaxyx( stdscr, row, col );

    if ( ( row > 6 ) && ( col > 19 ) ) {
        mvhline( row-4, 2, '=', col-4 );
        mvhline( row-3, 2, ' ', col-4 );
        mvhline( row-2, 2, '=', col-4 );
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
void dhline( int r, int c, int len ) {
    mvhline( r, c + 1, HOR, len - 1 );
    mvprintw( r, c, EDG );
    mvprintw( r, c + len, EDG );
}

/**
 * Draw a vertical line
 */
void dvline( int r, int c, int len ) {
    mvhline( r + 1, c, VER, len - 2 );
    mvprintw( r, c, EDG );
    mvprintw( r + len, c, EDG );
}

/**
 * draw a box
 */
void drawbox( int r0, int c0, int r1, int c1 ) {
    dhline( r0, c0, c1 - c0 );
    dhline( r1, c0, c1 - c0 );
    mvvline( r0 + 1, c0, VER, r1 - r0 - 1 );
    mvvline( r0 + 1, c1, VER, r1 - r0 - 1 );
}

void drawframe( struct entry_t *current, const char *status, int stream ) {
    int i, maxlen, pos;
    int row, col;
    int middle;
    char buff[MAXPATHLEN];
    struct entry_t *runner;

    if( popUpActive() ) {
        return;
    }

    refresh();
    getmaxyx( stdscr, row, col );

    /* Keep a minimum size to make sure */
    if ( ( row > 6 ) && ( col > 19 ) ) {
        /* main frame */
        drawbox( 0, 1, row - 2, col - 2 );

        maxlen = col - 6;

        if( stream ) {
            middle=2;
        }
        else {
            middle=( row-1 )/2;
        }

        /* title */
        dhline( middle-1, 1, col-3 );

        if( NULL != current ) {
            strip( buff, current->album, maxlen );
        }
        else {
            strip( buff, "mixplay "VERSION, maxlen );
        }

        pos = ( col - ( strlen( buff ) + 2 ) ) / 2;
        mvprintw( middle-1, pos, " %s ", buff );

        /* Set the current playing title */
        if( NULL != current ) {
            strip( buff, current->display, maxlen );

            if( current->flags & MP_FAV ) {
                attron( A_BOLD );
            }

        }
        else {
            strcpy( buff, "---" );
        }

        setTitle( buff );

        pos = ( col - strlen( buff ) ) / 2;
        mvhline( middle, 2, ' ', maxlen + 2 );
        mvprintw( middle, pos, "%s", buff );
        attroff( A_BOLD );

        dhline( middle+1, 1, col-3 );

        /* print the status */
        strip( buff, status, maxlen );
        pos = ( col - ( strlen( buff ) + 2 ) ) / 2;
        mvprintw( row - 2, pos, " %s ", buff );

        /* song list */
        if( NULL != current ) {
            /* previous songs */
            runner=current->plprev;

            for( i=middle-2; i>0; i-- ) {
                if( current != runner ) {
                    strip( buff, runner->display, maxlen );

                    if( runner->flags & MP_FAV ) {
                        attron( A_BOLD );
                    }

                    runner=runner->plprev;
                }
                else {
                    strcpy( buff, "---" );
                }

                mvhline( i, 2, ' ', maxlen + 2 );
                mvprintw( i, 3, "%s", buff );
                attroff( A_BOLD );
            }

            /* past songs */
            runner=current->plnext;

            for( i=middle+2; i<row-2; i++ ) {
                if( current != runner ) {
                    strip( buff, runner->display, maxlen );

                    if( runner->flags & MP_FAV ) {
                        attron( A_BOLD );
                    }

                    runner=runner->plnext;
                }
                else {
                    strcpy( buff, "---" );
                }

                mvhline( i, 2, ' ', maxlen + 2 );
                mvprintw( i, 2, "%s", buff );
                attroff( A_BOLD );
            }
        }
    }

    refresh();
}

/*
 * dummy implementations
 */
void progressStart( char *msg, ... ) {
	addMessage( 0, msg );
}

void progressEnd( char *msg ) {
	addMessage( 0, msg );
}

void updateUI( mpconfig *control ) {
    char status[MP_MSGLEN];
    int i;

    if( isendwin() ) {
    	control->status = mpc_quit;
    	return;
    }
	if( control->status == mpc_idle ) {
		sprintf( status, " STOP/PAUSE " );
	}
	else {
		if( control->playstream ) {
			sprintf( status, "%s PLAYING", control->playtime );
		}
		else {
			sprintf( status, "%s [", control->playtime );
			for( i=0; i<30; i++ ) {
				if( i < ( ( control->percent * 3 ) / 10 ) ) {
					status[i+7]='=';
				}
				else if( i == ( ( control->percent * 3 ) / 10 ) ) {
					status[i+7]='>';
				}
				else {
					status[i+7]=' ';
				}
			}
			status[i+7]=0;
			strcat( status, "] " );
			strcat( status, control->remtime );
		}
	}
	drawframe( control->current, status, control->playstream );

	if( getMessage( status ) ) {
		popUp( 1, status );
    }
}
