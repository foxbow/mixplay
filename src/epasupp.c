/**
 * support for a 2.7" ePaper display
 *
 * This uses a 'paper' format, so 0/0 is the lower left corner, not the upper
 * left as usually in a display. This may change though...
 */

#include "utils.h"
#include "epasupp.h"
#include "bmfont.h"
#include <stdlib.h>
#include <stdio.h>
#include <wiringPiSPI.h>
#include <errno.h>

static unsigned char _bm_red[EPDBYTES];
static unsigned char _bm_black[EPDBYTES];
static pthread_mutex_t _powerlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _idlelock=PTHREAD_MUTEX_INITIALIZER;

/*
  -2 - not initialized
  -1 - initializing
   0 - powered down
   1 - powered up
*/
static volatile int  _state=-2;

/**
 * these are confusing as the current layout zeroes on the lower left corner
 * not the upper left - see above
 */
static const unsigned char _epsymbols[ep_max][32]={
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
	{ 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x3c, 0x00, 0xfc, 0x00, 0xfc, 0x03, 0xfc, 0x0f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x0f, 0xfc, 0x03, 0xfc, 0x00, 0x3c, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x3c, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0xfc, 0x3f, 0xfc, 0x3f, 0xf8, 0x1f, 0xf8, 0x1f, 0xf0, 0x0f, 0xf0, 0x0f, 0xe0, 0x07, 0xe0, 0x07, 0xc0, 0x03, 0xc0, 0x03, 0x80, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x80, 0x01, 0xc0, 0x03, 0xc0, 0x03, 0xe0, 0x07, 0xe0, 0x07, 0xf0, 0x0f, 0xf0, 0x0f, 0xf8, 0x1f, 0xf8, 0x1f, 0xfc, 0x3f, 0xfc, 0x3f, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x0c, 0x30, 0x0c, 0x3c, 0x0c, 0x3f, 0xcc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xcc, 0x3f, 0x0c, 0x3f, 0x0c, 0x3c, 0x0c, 0x30, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x0c, 0x30, 0x3c, 0x30, 0xfc, 0x30, 0xfc, 0x33, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfc, 0x33, 0xfc, 0x30, 0x3c, 0x30, 0x0c, 0x30, 0x00, 0x00, 0x00, 0x00 },
	{ 0x00, 0x00, 0x80, 0x01, 0xc0, 0x03, 0xe0, 0x07, 0xf0, 0x0f, 0xf8, 0x1f, 0xfc, 0x3f, 0xfc, 0x3f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfe, 0x7f, 0xfc, 0x3f, 0x7c, 0x3e, 0x10, 0x08, 0x00, 0x00 },
	{ 0x00, 0x00, 0x1c, 0x38, 0x70, 0x0e, 0xc0, 0x03, 0x70, 0x0e, 0x1c, 0x38, 0xc0, 0x03, 0x60, 0x05, 0xe0, 0x07, 0x70, 0x0e, 0xf8, 0x1f, 0xb8, 0x1d, 0x98, 0x19, 0xf8, 0x1f, 0xe0, 0x07, 0x00, 0x00 }
};

/* Lookup tables */
static const unsigned char lut_vcom_dc[] = {
    0x00, 0x00,
    0x00, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R21H  00 = - */
static const unsigned char lut_ww[] = {
    0x90, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x40, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x80, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R22H  10 = red */
static const unsigned char lut_bw[] = {
    0xA0, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x90, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0xB0, 0x04, 0x10, 0x00, 0x00, 0x05,
    0xB0, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0xC0, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R23H 11 = white */
static const unsigned char lut_bb[] = {
    0x90, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x40, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x80, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R24H 01 = black */
static const unsigned char lut_wb[] = {
    0x90, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x20, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x10, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/*
 * communicate with the display
 * cmd      - command/data to send
 * datamode - 0:command, 1:data
 */
static void epsSend( unsigned char cmd, int datamode ) {
  digitalWrite( DC_PIN, datamode );
  digitalWrite( CS_PIN, 0);
  if( wiringPiSPIDataRW( 0, &cmd, 1 ) < 0 ) {
		/* is a command fails this is fail worthy.. */
    fail( errno, "wiringPiSPIDataRW(0,cmd,1) failed" );
  }
  digitalWrite( CS_PIN, 1);
}

/*
 * unlock the display functions
 */
static void epsUnidle(){
	addMessage(2,"EPS: idle unlocked!");
	pthread_mutex_unlock( &_idlelock );
}

/*
 * wait for ePaper
 * This should probably become epsLock()
 */
static void epsIdle(int unlock) {
	addMessage(2,"EPS: idle locked!");
	pthread_mutex_lock( &_idlelock );
  if(  _state == -2 ) {
    addMessage( 1, "EPS: Idling on unitialized Screen!" );
  }
  if(  _state == 0 ) {
    addMessage( 2, "EPS: Idling on powered off Screen!" );
  }
	if(  _state <= 0 ) {
		addMessage( 2, "EPS: Idling on initialization (%i)",  _state );
    while(  _state != 1 ) {
      delay(100);
    }
    addMessage( 2, "EPS: initialized" );
	}
  if( digitalRead( BSY_PIN ) == 0 ) {
    addMessage( 2, "EPS: busy" );
    while( digitalRead( BSY_PIN ) == 0 ) {
      delay(100);
    }
    addMessage( 2, "EPS: ready" );
  }
	if( unlock != 0 ) {
		epsUnidle();
	}
	addMessage(2,"EPS: exit idle..");
}

/*
 * pushes the reset button on the display.
 */
static void epsReset( void ) {
  digitalWrite( RST_PIN, 1 );
  delay( 200 );
  digitalWrite( RST_PIN, 0 );
  delay( 200 );
  digitalWrite( RST_PIN, 1 );
  delay( 200 );
}

/*
 * sends the bitmaps to the display.
 *
 * These are interpreted as 0x and x0, so it is not, however 00 is set to 11
 * so unset pixels (not red or black) are forced white and not unchanged..
 * Not sure if it's due to the wrong command being send or due to the
 * set up of the lookup tables.
 */
void epsDisplay( ) {
	unsigned i;
	if (  _state == -2 ) {
		addMessage( 0, "EPS: Display is not initialized!");
		return;
	}
	else if(  _state == -1 ) {
		addMessage( 1, "EPS: Display is initializing.." );
		return;
	}
	else if(  _state == 0 ) {
		addMessage( 2, "EPS: Waking up display to draw" );
		if( epsPoweron() ) {
			return;
		}
	}

	epsIdle(0);

	epsSend( DATA_START_TRANSMISSION_1, 0 );
	for( i=0; i<EPDBYTES; i++ ) {
		epsSend( _bm_black[i], 1 );
	}
	epsSend( DATA_STOP, 0 );

	epsSend( DATA_START_TRANSMISSION_2, 0 );
	for( i=0; i<EPDBYTES; i++ ) {
		epsSend( _bm_red[i], 1 );
	}
	epsSend( DATA_STOP, 0 );

	epsSend( DISPLAY_REFRESH, 0 );
	epsUnidle();
}

/* send coordinates in partial update format */
static void epsSendCoords( unsigned x, unsigned y, unsigned w, unsigned h ) {
	epsSend( x&0x0100 >> 8, 1 );
	epsSend( x&0x00f8, 1 );
	epsSend( y&0x0100 >> 8, 1 );
	epsSend( y&0x00ff, 1 );
	epsSend( w&0x0100 >> 8, 1 );
	epsSend( w&0x00f8, 1 );
	epsSend( h&0x0100 >> 8, 1 );
	epsSend( h&0x00ff, 1 );
}

/* This is tricky as x and y are not what the display expects */
void epsPartialDisplay( unsigned x, unsigned y, unsigned w, unsigned h ) {
	unsigned bmx, bmy, bmw, bmh;
	unsigned row, col;

	/* w and l are width and height related, not coordinates! */
	if( ( x+w > EPHEIGHT ) || ( y+h > EPWIDTH ) ) {
			addMessage( 0, "EPS: Area (%u,%u,%u,%u) is out of area!", x, y, w, h );
			return;
	}

	if(  _state < 0 ) {
		addMessage( 0, "EPS: PartialDisplay is not initialized!" );
		return;
	}
	else if(  _state == 0 ) {
		addMessage( 2, "EPS: Waking up display to draw" );
		if( epsPoweron() ) {
			return;
		}
	}

	addMessage( 2, "CO (%u,%u)/(%u/%u)", x, y, w, h );
	/* transform coordinates from Display to bitmap */
	bmx=EPWIDTH-(y+h);
	bmw=h;
	bmy=EPHEIGHT-(x+w);
	bmh=w;

	/* tweak bitmap offsets to fit definition */
	if( bmx%8 != 0 ) {
		bmw+=( bmx%8 );
		bmx=bmx&0x01f8;
	}
	if( bmw%8 != 0 ) {
		bmw=(bmw+8)&0x01f8;
	}

	if( bmy & 1 ) {
		bmy--;
		bmh++;
	}
	if( bmh & 1 ) {
		bmh++;
	}

	addMessage( 2, "BM(%u,%u)/(%u/%u)", bmx, bmy, bmw, bmh );

	epsIdle(0);

	epsSend( PARTIAL_DATA_START_TRANSMISSION_1, 0 );
	epsSendCoords( bmx, bmy, bmw, bmh );
	for( row=bmy; row < bmy+bmh; row++ ) {
		for( col=(bmx/8); col<(bmx+bmw)/8; col++ ) {
			epsSend( _bm_black[(Y_BYTES)*row+col], 1 );
		}
	}
	epsSend( DATA_STOP, 0 );

	epsSend( PARTIAL_DATA_START_TRANSMISSION_2, 0 );
	epsSendCoords( bmx, bmy, bmw, bmh );
	for( row=bmy; row < bmy+bmh; row++ ) {
		for( col=(bmx/8); col<(bmx+bmw)/8; col++ ) {
			epsSend( _bm_red[(Y_BYTES)*row+col], 1 );
		}
	}
	epsSend( DATA_STOP, 0 );

	epsSend( PARTIAL_DISPLAY_REFRESH, 0 );
	epsSendCoords( bmx, bmy, bmw, bmh );

	epsUnidle();
}

void epsSetPixel( epsmap map, unsigned x, unsigned y ) {
  unsigned pos=0;

  /* clipping */
  if( ( x > X_MAX ) || ( y > Y_MAX ) ) {
    addMessage( 2, "EPS: setpixel %u, %u is out of range!", x, y );
    return;
  }
	/* transform to actual layout.. */
	y=Y_MAX-y;
	x=X_MAX-x;

  pos=(22*x)+(y/8);
	if( map & epm_black ) {
  	_bm_black[pos]|=(128>>(y%8));
	}
	if( map & epm_red ) {
  	_bm_red[pos]|=(128>>(y%8));
	}
}

static void epsUnsetPixel( epsmap map, unsigned x, unsigned y ) {
  unsigned pos=0;

  if( ( x > X_MAX ) || ( y > Y_MAX ) ) {
    addMessage( 2, "EPS: unsetpixel %u, %u is out of range!", x, y );
    return;
  }
	/* transform to actual layout.. */
	y=Y_MAX-y;
	x=X_MAX-x;

  pos=(22*x)+(y/8);
	if( map & epm_black ) {
  	_bm_black[pos]&=~(128>>(y%8));
	}
	if( map & epm_red ) {
  	_bm_red[pos]&=~(128>>(y%8));
	}
}

void epsWipe( epsmap map, unsigned x, unsigned y, unsigned w, unsigned l ) {
	int i, j;
	for( i=x; i<x+l; i++ ) {
		for( j=y; j<y+w; j++ ) {
			epsUnsetPixel( map, i, j );
		}
	}
}

void epsWipeFull( epsmap map ) {
	int i;
	for( i=0; i<EPDBYTES; i++ ) {
		if( map & epm_black ) {
			_bm_black[i]=0;
		}
		if( map & epm_red ) {
			_bm_red[i]=0;
		}
	}
}

static void epsPutByte( epsmap map, unsigned posx, unsigned posy, unsigned char b, int mag ) {
	int i, j;
	int m=(mag>0)?2:1;

	if( ( posx > X_MAX ) || ( posy > Y_MAX ) ) {
		return;
	}

	for( i=0; i<8; i++ ) {
		if( b & ( 128 >> i ) ) {
			for( j=0; j<m; j++ ) {
				epsSetPixel( map, posx+(m*i)+j, posy );
			}
		}
	}
}

int epsDrawChar( epsmap map, unsigned posx, unsigned posy, unsigned char c, int mag ) {
	int i, m, ym=(mag==2)?2:1;

	if( ( mag < 0 ) || ( mag > 2 ) ) {
		addMessage( 0, "EPS: illegal magnification of %i ('%c')!", mag, c );
		return 1;
	}

	c=c-' ';
	if( ( c < 0 ) || ( c > 94 ) ) {
		addMessage( 2 , "EPS: character %i is out of range!", c );
		return 1;
	}

	if( ( posx > X_MAX ) || ( posy > Y_MAX ) ) {
		return 1;
	}

	for( i=0; i<13; i++ ) {
		for( m=0; m<ym; m++ ) {
			epsPutByte( map, posx, posy+(i*ym)+m, _epfont[c][i], mag );
		}
	}

	return 0;
}

void epsDrawString( epsmap map, unsigned posx, unsigned posy, char *txt, int mag ) {
	unsigned end=0;
	int i, ill=0;
	int m=(mag>0)?2:1;

	if( ( mag < 0 ) || ( mag > 2 ) ) {
		addMessage( 0, "EPS: illegal magnification of %i (%s)!", mag, txt );
		return;
	}

	if( ( posx > X_MAX ) || ( posy > Y_MAX ) ) {
		return;
	}

	/* try to center short strings.. */
	end=posx+(strlen(txt)*9*m);
	if( end < X_MAX ) {
		posx=posx+((X_MAX-end)/2);
	}

	for( i=0; i<strlen(txt); i++ ) {
		ill+=epsDrawChar( map, posx+(9*(i-ill)*m), posy, txt[i], mag );
	}
}

/*
 * draw one of the predefined Symbols
 */
void epsDrawSymbol( epsmap map, unsigned posx, unsigned posy, epsymbol sym ) {
	unsigned x, y;
	if( sym >= ep_max ) {
		addMessage( 0, "EPS: Illegal symbol #%i!", sym );
		return;
	}

	if( ( posx > X_MAX ) || ( posy > Y_MAX ) ) {
		return;
	}

	for( x=0; x<16; x++ ){
		for( y=0; y<16; y++ ) {
			if( _epsymbols[sym][(x/8)+(2*y)] & (1<<(x%8)) ) {
				epsSetPixel( map, posx+x, posy+y );
			}
		}
	}
}

/*
 * draw a line in the given bitmap
 */
void epsLine( epsmap map, int x0, int y0, int x1, int y1 ) {
	int xs, xe, ys, ye;
	int x,y;
	int step;

	/* iterate over x or y? */
	if( abs(y1-y0) < abs(x1-x0) ) {
		/* always draw from left to right */
		if( x0<x1 ) {
			xs=x0;
			ys=y0;
			xe=x1;
			ye=y1;
		}
		else {
			xs=x1;
			ys=y1;
			xe=x0;
			ye=y0;
		}
		if( ys == ye ) {
			step=0;
		}
		else {
			step=(100*(ye-ys))/(xe-xs);
		}
		for( x=xs; x<=xe; x++ ) {
			y=ys+(((x-xs)*step)/100);
			epsSetPixel( map, x, y );
			epsSetPixel( map, x, y+1 );
		}
	}
	/* iterate over y */
	else {
		/* always draw from left to right */
		if( y0<y1 ) {
			xs=x0;
			ys=y0;
			xe=x1;
			ye=y1;
		}
		else {
			xs=x1;
			ys=y1;
			xe=x0;
			ye=y0;
		}
		if( xs == xe ) {
			step=0;
		}
		else {
			step=(100*(xe-xs))/(ye-ys);
		}
		for( y=ys; y<=ye; y++ ) {
			x=xs+(((y-ys)*step)/100);
			epsSetPixel( map, x, y );
			epsSetPixel( map, x+1, y );
		}
	}
}

/*
 * draw a box in the given bitmap
 */
void epsBox( epsmap map, unsigned x0, unsigned y0, unsigned x1, unsigned y1, int filled ) {
  unsigned x;
  if( filled ) {
    if( x0 < x1 ) {
      for( x=x0; x<=x1; x++ ) {
        epsLine( map, x, y0, x, y1 );
      }
    }
    else {
      for( x=x1; x<=x0; x++ ) {
        epsLine( map, x, y0, x, y1 );
      }
    }
  }
  else {
    epsLine( map, x0, y0, x1, y0 );
    epsLine( map, x1, y0, x1, y1 );
    epsLine( map, x1, y1, x0, y1 );
    epsLine( map, x0, y1, x0, y0 );
  }
}

/*
 * link one of the display buttons to a callback
 */
void epsButton( unsigned key, void(*func)(void) ){
	epsIdle(1);
	pinMode( key, INPUT );
	pullUpDnControl( key, PUD_UP ) ;
	wiringPiISR ( key, INT_EDGE_FALLING, func );
	epsUnidle();
}

/*
 * turn on the display. Use this after epsPoweroff() to turn the paper back on
 * Do not use epsSetup() for this!
 *
 * This starts the actual power on sequence in an extra thread as it will
 * take a couple of seconds which can be used better elsewhere. The rest of
 * the ESP functions take care of the state.
 */
int epsPoweron( void ) {
	int error=0;
	unsigned count;

	pthread_mutex_lock( &_powerlock );
  switch( _state ){
    case 0:
			epsReset();
		  addMessage( 2, "EPS: powering on" );
		  epsSend( POWER_ON, 0 );
			while( digitalRead( BSY_PIN ) == 0 ) {
				delay(100);
			}

			epsSend( PANEL_SETTING, 0 );
			epsSend( 0xaf,1 );        /* KW-BF   KWR-AF    BWROTP 0f               */

		  epsSend( PLL_CONTROL, 0 );
		  epsSend( 0x3a, 1 );       /* 3A 100HZ   29 150Hz 39 200HZ    31 171HZ  */

		  epsSend( POWER_SETTING, 0 );
		  epsSend( 0x03, 1 );                  /* VDS_EN, VDG_EN                   */
		  epsSend( 0x00, 1 );                  /* VCOM_HV, VGHL_LV[1], VGHL_LV[0]  */
		  epsSend( 0x2b, 1 );                  /* VDH                              */
		  epsSend( 0x2b, 1 );                  /* VDL                              */
		  epsSend( 0x09, 1 );                  /* VDHR                             */

		  epsSend( BOOSTER_SOFT_START, 0 );
		  epsSend( 0x07, 1 );
		  epsSend( 0x07, 1 );
		  epsSend( 0x17, 1 );

		  /* Power optimization */
		  epsSend( POWER_OPT, 0 );
		  epsSend( 0x60, 1 );
		  epsSend( 0xA5, 1 );
		  epsSend( POWER_OPT, 0 );
		  epsSend( 0x89, 1 );
		  epsSend( 0xA5, 1 );
		  epsSend( POWER_OPT, 0 );
		  epsSend( 0x90, 1 );
		  epsSend( 0x00, 1 );
		  epsSend( POWER_OPT, 0 );
		  epsSend( 0x93, 1 );
		  epsSend( 0x2A, 1 );
		  epsSend( POWER_OPT, 0 );
		  epsSend( 0x73, 1 );
		  epsSend( 0x41, 1 );

		  epsSend( VCM_DC_SETTING_REGISTER, 0 );
		  epsSend( 0x12, 1 );
		  epsSend( VCOM_AND_DATA_INTERVAL_SETTING, 0 );
		  epsSend( 0x87, 1 );        /* define by OTP */

		  /* set lookup tabels */
		  addMessage(2, "EPS setting lookup tables" );
		  epsSend( LUT_FOR_VCOM, 0 );                            /* vcom */
		  for(count = 0; count < 44; count++) {
		      epsSend(lut_vcom_dc[count],1);
		  }

		  epsSend( LUT_WHITE_TO_WHITE, 0 );                      /* ww -- */
		  for(count = 0; count < 42; count++) {
		      epsSend(lut_ww[count],1);
		  }

		  epsSend( LUT_BLACK_TO_WHITE, 0 );                      /* bw r  */
		  for(count = 0; count < 42; count++) {
		      epsSend(lut_bw[count],1);
		  }

		  epsSend( LUT_WHITE_TO_BLACK, 0 );                      /*wb w */
		  for(count = 0; count < 42; count++) {
		      epsSend(lut_bb[count],1);
		  }

		  epsSend( LUT_BLACK_TO_BLACK, 0 );                     /*bb b */
		  for(count = 0; count < 42; count++) {
		      epsSend(lut_wb[count],1);
		  }

			_state=1;
    break;
    case 1:
      /* cool, just go on */
    break;
    default:
      addMessage( 0, "EPS: Display is not initialized!" );
			error=-1;
  }
	pthread_mutex_unlock( &_powerlock );
	return error;
}

/*
 * the basic set-up which initializes the Raspberry Pi and is independant
 * of the state of the display
 */
void epsSetup( void ) {
  addMessage( 1, "EPS: Initializing ePaper");
	if(  _state != -2 ) {
		addMessage( 0, "EPS: Display is already set up!");
		return;
	}
  _state=-1;

	/* init wiringpi to use own pin layout */
	wiringPiSetup();

	/* init GPIOs for paper */
  pinMode( RST_PIN, OUTPUT);
  pinMode(  DC_PIN, OUTPUT);
  pinMode(  CS_PIN, OUTPUT);
	pinMode( BSY_PIN,  INPUT);

  if( wiringPiSPISetupMode(0, 32000000, 0) < 0 ) {
    addMessage( 0, "EPS: Could not set SPI mode!" );
    return;
  }
   _state=0;
  epsPoweron();
}

/*
 * returns the current state of the ePaper
 */
int epsGetState() {
	return  _state;
}

/*
 * turn off the display
 */
void epsPoweroff(void) {
	if(  _state == 1 ) {
		epsIdle(0); /* this may be a bad idea tho.. */
		epsSend( VCOM_AND_DATA_INTERVAL_SETTING, 0 );
		epsSend( 0xf7, 1 );
		epsSend( POWER_OFF, 0 );
		epsSend( DEEP_SLEEP, 0 );
		epsSend( 0xA5, 1 );
		epsUnidle();
		addMessage( 2, "EPS: ePaper turned off");
		_state=0;
  }
}
