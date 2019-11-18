/*
 * ePaper integration.
 */
#include <stdio.h>
#include <sys/time.h>
#include <assert.h>

#include "mpepa.h"
#include "config.h"
#include "player.h"
#include "utils.h"

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

/*
 * the last known state. Used to avoid updating too much.
 * set to mpc_start to force update.
 */
static mpcmd_t _last=mpc_start;
static int _updating=0;
static struct timeval _lastevent;
static pthread_mutex_t _debouncelock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _updatelock=PTHREAD_MUTEX_INITIALIZER;
static unsigned _ucount=0;
static char _lasttitle[MAXPATHLEN+1];
static _btmode_t _btmode=bt_noinit;
static _umode_t _umode=um_full;

/*
 * update the display with the current title and buttons
 * runs in it's own thread to not block updating
 */
static void *_update( ) {
	mpplaylist_t *current=NULL;

	blockSigint();

	while( getConfig()->status != mpc_quit ) {
		pthread_mutex_lock( &_updatelock );
		/* don't update on exit! */
		if( getConfig()->status == mpc_quit ) {
			break;
		}
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

			if( _last != mpc_idle ) {
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

		switch( _btmode ) {
			case bt_noinit:
			/* no break */
			case bt_dbplay: /* -/next/dnp */
			epsDrawSymbol( epm_black, 5, 90, ep_next );
			epsDrawSymbol( epm_black, 5, 30, ep_dnp );
			break;
			case bt_stream: /* -/^/v */
			epsDrawSymbol( epm_black, 5, 90, ep_up );
			epsDrawSymbol( epm_black, 5, 30, ep_down );
			break;
			default:
			addMessage( 0, "EP: update illegal epmode %i", _btmode );
		}

		/* update full or partial depending on mode */
		switch( _umode ) {
			case um_none:
				/* no update.. */
			break;
			case um_play: /* play/pause */
				epsPartialDisplay( 5, 150, 16, 16 );
			break;
			case um_buttons: /* buttons */
				epsPartialDisplay( 5, 30, 16, 76 );
			break;
			case um_icons: /* play and buttons */
				epsPartialDisplay( 5, 30, 16, 141 );
			break;
			default: /* full */
				epsDisplay( );
		}

		_umode=um_none;
		_updating=0;
	}
	return NULL;
}

/**
 * special handling for the server during information updates
 */
void epUpdateHook( ) {
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
	if( getConfig()->status != _last ) {
		_last=getConfig()->status;
		_umode|=um_play;
	}

	/* has the title changed? */
	if ( ( title != NULL ) && ( strcmp( title, _lasttitle ) != 0 ) ) {
		strtcpy( _lasttitle, title, MAXPATHLEN );
		/* also get the status to avoid double draw on start */
		_last=getConfig()->status;
		_umode|=um_title;
	}

	/* disable DNP/FAV on stream play */
	if( ( getConfig()->mpmode != PM_DATABASE ) && ( _btmode != bt_stream ) ) {
		_btmode=bt_stream;
		_umode|=um_title;
		_umode|=um_icons;
	}

	/* do we need an update? */
	if( _umode != um_none ) {
		if( _ucount > TOSLEEP ) {
			addMessage( 2, "EP: Display sleeps!" );
			if( epsPoweron() ) {
				/* display did not wake up - shame! */
				_updating=0;
				return;
			}
		}
		_ucount=0;
		/* unlock update thread */
		pthread_mutex_unlock(&_updatelock);
	}
	else {
		_updating=0;
	}

	if( _ucount == TOSLEEP ) {
		addMessage( 2, "EP: Send Display to sleep.." );
		epsPoweroff();
		_ucount++;
	}
	else if( ( epsGetState() >= 0 ) && ( _ucount < TOSLEEP ) ) {
		_ucount++;
	}
}

/*
 * debounce the buttons
 */
static void debounceCmd( mpcmd_t cmd ) {
	struct timeval now, diff;
	if( pthread_mutex_trylock( &_debouncelock ) ) {
		addMessage( 2, "EP: mutex debounce %s", mpcString( cmd ) );
		return;
	}
	gettimeofday( &now, NULL );
	timersub( &now, &_lastevent, &diff );
	_lastevent.tv_sec=now.tv_sec;
	_lastevent.tv_usec=now.tv_usec;

	if( ( diff.tv_sec > 0 ) || ( diff.tv_usec > 200000 ) ) {
		addMessage( 2, "EP: cmd %s", mpcString( cmd ) );
		setCommand( cmd, NULL );
	}
	else {
		addMessage( 2, "EP: debounce %s", mpcString( cmd ) );
		addMessage( 1, "EP: tv %u - %u", (unsigned)diff.tv_sec, (unsigned)diff.tv_usec );
	}
	pthread_mutex_unlock( &_debouncelock );
}

/*
 * Play/Pause button
 */
static void key1_cb( void ) {
	if( _btmode == bt_noinit ) {
		addMessage( 0, "EP: Key1, not yet.." );
		return;
	}

	debounceCmd( mpc_play );
}

/*
 * Key2: next/vol+
 */
static void key2_cb( void ) {
	switch( _btmode ) {
		case bt_noinit:
			addMessage( 0, "EP: Key2, not yet.." );
		break;
		case bt_dbplay:
			debounceCmd(mpc_next);
		break;
		case bt_stream:
			debounceCmd(mpc_ivol);
		break;
		default:
			addMessage( 0, "EP: Unknown epMode %i for button2!", _btmode );
	}
}

/*
 * Key3: DNP, Vol-
 */
static void key3_cb( void ) {
	switch( _btmode ) {
		case bt_noinit:
			addMessage( 0, "EP: Key3, not yet.." );
		break;
		case bt_dbplay:
			debounceCmd( mpc_dnp|mpc_display );
		break;
		case bt_stream:
			debounceCmd(mpc_dvol);
		break;
		default:
			addMessage( 0, "EP: Unknown epMode %i for button3!", _btmode );
	}
}

/*
 * Draw a little heart in the center of the display and switch it off
 */
void epExit( void ) {
	/* this must not be called on anything but exit */
	assert( getConfig()->status == mpc_quit );
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
	/* DO NOT USE KEY4, it will break the HiFiBerry function!
		 However it will act like a MUTE button as is... */
	_btmode=0;
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
