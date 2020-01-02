#ifndef _MPEPA_H_
#define _MPEPA_H_ 1
#include "epasupp.h"

#define TOSLEEP 25

/* enable partial updates */
#define EPS_PARTIAL 1

/* the button mode */
typedef enum {
	bt_noinit=-1,
	bt_dbplay=0,
	bt_stream=1
} _btmode_t;

/* update mode */
typedef enum {
	um_full=-1,
	um_none=0,
	um_play=1,
	um_buttons=2,
	um_icons=3,		/* play and buttons */
	um_title=4
} _umode_t;

void epSetup( void );
void epUpdateHook( );
void epExit( void );
#endif
