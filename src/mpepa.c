/*
 * ePaper integration.
 */
#include <stdio.h>
#include "epasupp.h"
#include "config.h"
#include "player.h"
#include "utils.h"

/* the button modes
   button 1 cycles the modes for button 2 and 3
    0 - default (pause/next)
    1 - volume  (up/down)
    2 - title   (DNP/FAV)
*/
static int epmode=-1;
/*
 * the last known state. Used to avoid updating too much.
 * set to mpc_start to force update.
 */
static mpcmd last=mpc_start;
static int _updating=0;

static void debounceCmd( mpcmd cmd ) {
  if( getConfig()->command == mpc_idle ) {
    addMessage(1,"EP cmd %s", mpcString( cmd ) );
    setCommand( cmd );
  }
  else {
    addMessage(1,"EP debounce %s", mpcString( cmd ) );
  }
}

/* cycle through the modes */
static void key1_cb( void ) {
  if( epmode == -1 ) {
    addMessage(0, "Not yet.." );
    return;
  }
  epmode=(epmode+1)%3;
  last=mpc_start;
}

static void key2_cb( void ) {
  switch( epmode ) {
    case -1:
      addMessage(0, "Not yet.." );
    break;
    case 0:
      debounceCmd(mpc_play);
    break;
    case 1:
      debounceCmd(mpc_ivol);
    break;
    case 2:
      debounceCmd( mpc_dnp|mpc_display );
    break;
    default:
      addMessage(0,"Unknown epMode %u for button2!",epmode);
  }
}

static void key3_cb( void ) {
  switch( epmode ) {
    case -1:
      addMessage(0, "Not yet.." );
    break;
    case 0:
      debounceCmd(mpc_next);
    break;
    case 1:
      debounceCmd(mpc_dvol);
    break;
    case 2:
      debounceCmd( mpc_fav|mpc_display );
    break;
    default:
      addMessage(0,"Unknown epMode %u for button2!",epmode);
  }
}

void epExit( void ) {
  unsigned char *black=falloc(EPDBYTES,1);
  epsDrawSymbol( black, 124, 80, ep_fav );
  epsDisplay( NULL, black );
  free(black);
  epsPoweroff();
}

PI_THREAD(_setButtons) {
  epsIdle();
  epsButton( KEY1, key1_cb );
  epsButton( KEY2, key2_cb );
  epsButton( KEY3, key3_cb );
  /* DO NOT USE BUTTON4, it will break the HiFiBerry function!
     However it will act like a MUTE button as is... */
  epmode=0;
  return NULL;
}

void epSetup() {
  epsSetup();
  /* init buttons */
  if( piThreadCreate(_setButtons) != 0 ) {
    addMessage( 0, "EP: Could not start button init thread!" );
    return;
  }
}

PI_THREAD(_update) {
  int i;
  unsigned char *black=falloc(EPDBYTES,1);
  addMessage(1,"EP update do");
  last=getConfig()->status;
  if( last == mpc_play ) {
    for( i=0; i<40; i+=2 ) {
  		epsLine( black, 108+i, 20, 183+i, 88 );
  		epsLine( black, 183+i, 88, 108+i, 156 );
  	}
  	epsLine( black, 183, 88, 223, 88 );
  }
  else {
    epsBox( black, 118, 20, 158, 156, 1 );
    epsBox( black, 194, 20, 234, 156, 1 );
  }

  switch( epmode ) {
    case 0: /* pause/next */
    epsDrawSymbol( black, 5, 90, ep_play );
    epsDrawSymbol( black, 5, 30, ep_next );
    break;
    case 1: /* volume */
    epsDrawSymbol( black, 5, 90, ep_up );
    epsDrawSymbol( black, 5, 30, ep_down );
    break;
    case 2: /* dnp/fav */
    epsDrawSymbol( black, 5, 90, ep_dnp );
    epsDrawSymbol( black, 5, 30, ep_fav );
    break;
    default:
    addMessage( 0, "EP illegal mode %u", epmode );
  }

  epsDisplay( black, NULL );
  free(black);
  _updating=0;
  return NULL;
}

static unsigned ucount=0;
/**
 * special handling for the server during information updates
 */
void ep_updateHook( void *ignore ) {
  if( ( _updating == 0 ) && ( getConfig()->status != last ) ) {
    if( ucount > 1000 ) {
      addMessage( 0, "EP Display sleeps!" );
      epsPoweron();
    }
    ucount=0;
    _updating=1;
    if( piThreadCreate(_update) != 0 ) {
      addMessage( 0, "EP: Could not start update thread!" );
      return;
    }
  }
  if( ucount > 1000 ) {
    addMessage( 1, "EP Send Display to sleep.." );
    epsPoweroff();
  }
}
