/*
 * ePaper integration.
 */
#include <stdio.h>
#include <sys/time.h>
#include "mpepa.h"
#include "config.h"
#include "player.h"
#include "utils.h"
#define TOSLEEP 25

#define EPS_PARTIAL 1

/* the button modes
	 -1 - not initialized
	  0 - default (pause/next/DNP)
	  1 - stream  (pause/up/down)
*/
static int _epmode=-1;

/* update mode
	-1 - full
	 0 - none
	 1 - play/pause
	 2 - buttons
	 4 - title
*/
static int _umode=-1;

/*
 * the last known state. Used to avoid updating too much.
 * set to mpc_start to force update.
 */
static mpcmd last=mpc_start;
static int _updating=0;
static struct timeval lastevent;
static pthread_mutex_t _debouncelock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _updatelock=PTHREAD_MUTEX_INITIALIZER;
static unsigned ucount=0;
static char lasttitle[MAXPATHLEN+1];

/*
 * update the display with the current title and buttons
 * runs in it's own thread to not block updating
 */
static void *_update( ) {
	mpplaylist *current=NULL;

	blockSigint();

	while( getConfig()->command != mpc_quit ) {
		pthread_mutex_lock( &_updatelock );
		current=getConfig()->current;
		epsWipeFull( epm_both );

		if( current != NULL ) {
			if( current->prev != NULL ) {
				epsDrawString( epm_red, 40, 159, current->prev->title->display, 0 );
			}
			else {
				epsDrawString( epm_red, 40, 160, "---", 1 );
			}
			if( current->next != NULL ) {
				epsDrawString( epm_red, 40, 1, current->next->title->display, 0 );
			}
			else {
				epsDrawString( epm_red, 40, 0, "---", 0 );
			}

			if( last != mpc_idle ) {
				if( current->title->flags & MP_FAV ) {
					epsDrawSymbol( epm_red, 5, 150, ep_fav );
				}
				else {
					epsDrawSymbol( epm_red, 5, 150, ep_play );
				}
				epsDrawString( epm_black, 40, 120, current->title->artist, 1 );
				epsDrawString( epm_black, 40, 75,  current->title->title, 2 );
				epsDrawString( epm_black, 40, 40,  current->title->album, 0 );
			}
			else {
				epsDrawSymbol( epm_red, 5, 150, ep_pause );
				epsDrawString( epm_red, 40, 120, current->title->artist, 1 );
				epsDrawString( epm_red, 40, 75,  current->title->title, 2 );
				epsDrawString( epm_red, 40, 40,  current->title->album, 0 );
			}

		}
		else {
			epsDrawString( epm_red, 40, 50, "Initializing", 1 );
		}

		epsLine( epm_black, 0, 128, 30, 128 );
		epsLine( epm_black, 0, 66, 30, 66 );
		epsLine( epm_black, 30, 0, 30, Y_MAX );
		epsLine( epm_black, 30, 20, X_MAX, 20 );
		epsLine( epm_black, 30, 150, X_MAX, 150 );

		switch( _epmode ) {
			case -1:
			/* no break */
			case 0: /* -/next/dnp */
			epsDrawSymbol( epm_black, 5, 90, ep_next );
			epsDrawSymbol( epm_black, 5, 30, ep_dnp );
			break;
			case 1: /* -/^/v */
			epsDrawSymbol( epm_black, 5, 90, ep_up );
			epsDrawSymbol( epm_black, 5, 30, ep_down );
			break;
			default:
			addMessage( 0, "EP: update illegal epmode %i", _epmode );
		}

		/* update full or partial depending on mode */
		switch( _umode ) {
			case 0:
				/* no update.. */
			break;
			case 1: /* play/pause */
				epsPartialDisplay( 5, 150, 16, 16 );
			break;
			case 2: /* buttons */
				epsPartialDisplay( 5, 30, 16, 76 );
			break;
			case 3: /* play and buttons */
				epsPartialDisplay( 5, 30, 16, 141 );
			break;
			default: /* full */
				epsDisplay( );
		}

		_umode=0;
		_updating=0;
	}
	return NULL;
}

/**
 * special handling for the server during information updates
 */
void ep_updateHook( ) {
	char *title=NULL;
	if( epsGetState() < 0 ) {
		return;
	}
	/* no need to be really thread safe as only one thread can call this
	 * function and that has a frequency of ~1 call/s */
	if( _updating != 0 ) {
		return;
	}
	_updating=1;

	if( ( getConfig()->current!=NULL ) &&
	( getConfig()->current->title != NULL ) ) {
		title=getConfig()->current->title->display;
	}

	/* has the status changed? */
	if( getConfig()->status != last ) {
		last=getConfig()->status;
		_umode|=1;
	}

	/* has the title changed? */
	if ( ( title != NULL ) && ( strcmp( title, lasttitle ) != 0 ) ) {
		strtcpy( lasttitle, title, MAXPATHLEN );
		/* also get the status to avoid double draw on start */
		last=getConfig()->status;
		_umode|=4;
	}

	/* disable DNP/FAV on stream play */
	if( ( getConfig()->mpmode == PM_STREAM ) && ( _epmode != 3 ) ) {
		_epmode=3;
		_umode|=6;
	}

	/* do we need an update? */
	if( _umode != 0 ) {
		if( ucount > TOSLEEP ) {
			addMessage( 2, "EP: Display sleeps!" );
			if( epsPoweron() ) {
				/* display did not wake up - shame! */
				_updating=0;
				return;
			}
		}
		ucount=0;
		/* unlock update thread */
		pthread_mutex_unlock(&_updatelock);
	}
	else {
		_updating=0;
	}

	if( ucount == TOSLEEP ) {
		addMessage( 2, "EP: Send Display to sleep.." );
		epsPoweroff();
		ucount++;
	}
	else if( ( epsGetState() >= 0 ) && ( ucount < TOSLEEP ) ) {
		ucount++;
	}
}

/*
 * debounce the buttons
 */
static void debounceCmd( mpcmd cmd ) {
	struct timeval now, diff;
	if( pthread_mutex_trylock( &_debouncelock ) ) {
		addMessage( 2,"EP: mutex debounce %s", mpcString( cmd ) );
		return;
	}
	gettimeofday( &now, NULL );
	timersub( &now, &lastevent, &diff );
	lastevent.tv_sec=now.tv_sec;
	lastevent.tv_usec=now.tv_usec;

	if( ( diff.tv_sec > 0 ) || ( diff.tv_usec > 200000 ) ) {
		addMessage( 2,"EP: cmd %s", mpcString( cmd ) );
		setCommand( cmd );
	}
	else {
		addMessage( 2,"EP: debounce %s", mpcString( cmd ) );
		addMessage( 1,"EP: tv %u - %u", diff.tv_sec, diff.tv_usec );
	}
	pthread_mutex_unlock( &_debouncelock );
}

/*
 * cycle through the key modes on Button 1
 */
static void key1_cb( void ) {
	if( _epmode == -1 ) {
		addMessage( 0, "EP: Key1, not yet.." );
		return;
	}

	debounceCmd( mpc_play );
}

/*
 * Key2: next/vol+
 */
static void key2_cb( void ) {
	switch( _epmode ) {
		case -1:
			addMessage( 0, "EP: Key2, not yet.." );
		break;
		case 0:
			debounceCmd(mpc_next);
		break;
		case 1:
			debounceCmd(mpc_ivol);
		break;
		default:
			addMessage( 0, "EP: Unknown epMode %i for button2!", _epmode );
	}
}

/*
 * Key3: DNP, Vol-
 */
static void key3_cb( void ) {
	switch( _epmode ) {
		case -1:
			addMessage( 0, "EP: Key3, not yet.." );
		break;
		case 0:
			debounceCmd( mpc_dnp|mpc_display );
		break;
		case 1:
			debounceCmd(mpc_dvol);
		break;
		default:
			addMessage( 0, "EP: Unknown epMode %i for button3!", _epmode );
	}
}

/*
 * Draw a little heart in the center of the display and switch it off
 */
void epExit( void ) {
	/* allow the update thread to terminate */
	pthread_mutex_unlock(&_updatelock);

	if( epsGetState() == -2 ) {
		/* unitialized display, don't touch */
		return;
	}
	if( epsGetState() == 0 ) {
		if( epsPoweron() ){
			/* unable to power on? Then just ignore and hope for the best.. */
			return;
		}
	}
	epsWipeFull( epm_both );
	epsDrawSymbol( epm_red, 124, 80, ep_fav );
	epsDisplay( );
	epsPoweroff();
}

/*
 * helperfunction to set the buttons in an own thread
 * this is an own thread to not block on initialization
 * and PowerOn
 */
static void *_setButtons( ) {
	addMessage( 2, "New Thread: _setButtons()" );
	/* init buttons */
	epsButton( KEY1, key1_cb );
	epsButton( KEY2, key2_cb );
	epsButton( KEY3, key3_cb );
	/* DO NOT USE BUTTON4, it will break the HiFiBerry function!
		 However it will act like a MUTE button as is... */
	_epmode=0;
	addMessage( 2, "End Thread: _setButtons()" );
	return NULL;
}

/*
 * set up the display and initialize features
 */
void epSetup() {
	pthread_t tid;
	int error;
	epsSetup();

	/* run this in an own thread to allow initialization of the player
	 * while the display is still busy waking up */
	error=pthread_create( &tid, NULL, _setButtons, NULL );
	if( error != 0 ) {
		addMessage( 0, "EP: Could not start setup thread!" );
		addError(error);
		return;
	}

	/* start the background update thread. Failure to do this should be fatal! */
	error=pthread_create( &tid, NULL, _update, NULL );
	if( error != 0 ) {
		addMessage( 0, "EP: Could not start update thread!" );
		addError( error );
		return;
	}
}
