#ifndef NCBOX_H_
#define NCBOX_H_

#define HOR '-'
#define VER '|'
#define EDG "+"

#include "utils.h"
#include "musicmgr.h"

/**
 * curses helper functions
 */
void popUp( int time, const char *text, ... );
void popAsk( const char *text, char *reply );
void dhline( int r, int c, int len );
void dvline( int r, int c, int len );
void drawbox( int r0, int c0, int r1, int c1 );
int popUpActive( void );
void popDown( void );
void drawframe( mpplaylist *current, const char *status, int stream );
void nc_updateHook( void * );
/* void fail( int error, const char* msg, ... ); */

#endif /* NCBOX_H_ */