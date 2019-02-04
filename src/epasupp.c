#include "utils.h"
#include "epasupp.h"
#include <stdlib.h>
#include <stdio.h>
#include <wiringPiSPI.h>
#include <errno.h>

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

/*
  -2 - not initialized
  -1 - initializing
   0 - powered down
   1 - powered up
*/
static int state=-2;

/* Lookup tables */
const unsigned char lut_vcom_dc[] = {
    0x00, 0x00,
    0x00, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R21H */
const unsigned char lut_ww[] = {
    0x90, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x40, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x80, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R22H - red */
const unsigned char lut_bw[] = {
    0xA0, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x00, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x90, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0xB0, 0x04, 0x10, 0x00, 0x00, 0x05,
    0xB0, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0xC0, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R23H - white */
const unsigned char lut_bb[] = {
    0x90, 0x1A, 0x1A, 0x00, 0x00, 0x01,
    0x40, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x84, 0x0E, 0x01, 0x0E, 0x01, 0x10,
    0x80, 0x0A, 0x0A, 0x00, 0x00, 0x08,
    0x00, 0x04, 0x10, 0x00, 0x00, 0x05,
    0x00, 0x03, 0x0E, 0x00, 0x00, 0x0A,
    0x00, 0x23, 0x00, 0x00, 0x00, 0x01
};

/* R24H - black */
const unsigned char lut_wb[] = {
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
    fail( errno, "wiringPiSPIDataRW(0,cmd,1) failed" );
  }
  digitalWrite( CS_PIN, 1);
}

/*
 * wait for ePaper
 * This is BAAAD!!!
 */
void epsIdle() {
  if( state == -2 ) {
    addMessage( 2, "EP Idling on unitialized Screen!" );
  }
  if( state == 0 ) {
    addMessage( 2, "EP Idling on powered off Screen!" );
  }
	if( state <= 0 ) {
		addMessage( 1, "EP initializing" );
    while( state != 1 ) {
      delay(100);
    }
    addMessage( 1, "EP initialized" );
	}
  if( digitalRead( BSY_PIN ) == 0 ) {
    addMessage( 1, "EP busy" );
    while( digitalRead( BSY_PIN ) == 0 ) {
      delay(100);
    }
    addMessage( 1, "EP ready" );
  }
}

static void epsReset( void ) {
  digitalWrite( RST_PIN, 1 );
  delay( 200 );
  digitalWrite( RST_PIN, 0 );
  delay( 200 );
  digitalWrite( RST_PIN, 1 );
  delay( 200 );
}

void epsDisplay( unsigned char *black, unsigned char *red ) {
  unsigned i;
  epsIdle();

  epsSend( DATA_START_TRANSMISSION_1, 0 );
  for( i=0; i<EPDBYTES; i++ ) {
    if( black == NULL ) {
      epsSend(0,1);
    }
    else {
      epsSend( black[i], 1 );
    }
  }
  epsSend( DATA_STOP, 0 );

  epsSend( DATA_START_TRANSMISSION_2, 0 );
  for( i=0; i<EPDBYTES; i++ ) {
    if( red == NULL ) {
      epsSend(0,1);
    }
    else {
      epsSend( red[i], 1 );
    }
  }
  epsSend( DATA_STOP, 0 );

  epsSend( DISPLAY_REFRESH, 0 );
}

void epsSetPixel( unsigned char *map, unsigned x, unsigned y ) {
  unsigned pos=0;
	/* fix transformation off by one */
	x++;
	y++;
  /* clipping */
  if( ( x > EPHEIGHT ) || ( y > EPWIDTH ) ) {
    addMessage( 1, "EP pixel %u, %u is out or range!", x, y );
    return;
  }
	/* transform to actual layout.. */
	y=EPWIDTH-y;
	x=EPHEIGHT-x;

  pos=(22*x)+(y/8);
  map[pos]|=(128>>(y%8));
}

void epsDrawSymbol( unsigned char *map, unsigned posx, unsigned posy, epsymbol sym ) {
	unsigned x, y;
	for( x=0; x<16; x++ ){
		for( y=0; y<16; y++ ) {
			if( _epsymbols[sym][(x/8)+(2*y)] & (1<<(x%8)) ) {
				epsSetPixel( map, posx+x, posy+y );
			}
		}
	}
}

void epsLine( unsigned char* map, int x0, int y0, int x1, int y1 ) {
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

#if 0
void epsLine( unsigned char* map, unsigned x0, unsigned y0, unsigned x1, unsigned y1 ) {
  int dx=x1-x0;
  int dy=y1-y0;
  unsigned x, y;
  int step;

  /* straight horizontal line */
  if( dx==0 ) {
    if( y0<y1 ) {
      for( y=y0; y<=y1; y++ ) {
        epsSetPixel( map, x0, y );
      }
    }
    else {
      for( y=y1; y<=y0; y++ ) {
        epsSetPixel( map, x0, y );
      }  /* straight horizontal line */
  if( dx==0 ) {
    if( y0<y1 ) {
      for( y=y0; y<=y1; y++ ) {
        epsSetPixel( map, x0, y );
      }
    }
    else {
      for( y=y1; y<=y0; y++ ) {
        epsSetPixel( map, x0, y );
      }
    }
    return;
  }

  /* straight vertical line */
  if( dy==0 ) {
    if( x0<x1 ) {
      for( x=x0; x<=x1; x++ ) {
				epsSetPixel( map, x, y0 );
      }
    }
    else {
      for( x=x1; x<=x0; x++ ) {
        epsSetPixel( map, x, y0 );
      }
    }
    return;
  }

    }
    return;
  }

  /* straight vertical line */
  if( dy==0 ) {
    if( x0<x1 ) {
      for( x=x0; x<=x1; x++ ) {
				epsSetPixel( map, x, y0 );
      }
    }
    else {
      for( x=x1; x<=x0; x++ ) {
        epsSetPixel( map, x, y0 );
      }
    }
    return;
  }

  if( abs(dx) < abs(dy) ) {
    step=(100*dx)/dy;
    if( y0 < y1 ) {
      for( y=y0; y<=y1; y++ ) {
        x=x0+(((y-y0)*step)/100);
				epsSetPixel( map, x, y );
				epsSetPixel( map, x+1, y );
      }
    }
    else {
      for( y=y1; y<=y0; y++ ) {
        x=x1-(((y-y1)*step)/100);
        epsSetPixel( map, x, y );
				epsSetPixel( map, x+1, y );
      }
    }
  }
  else {
    step=(100*dy)/dx;
    if( x0 < x1 ) {
      for( x=x0; x<=x1; x++ ) {
        y=y0+(((x-x0)*step)/100);
        epsSetPixel( map, x, y );
				epsSetPixel( map, x, y+1 );
      }
    }
    else {
      for( x=x1; x<=x0; x++ ) {
        y=y1-(((x-x1)*step)/100);
				epsSetPixel( map, x, y );
				epsSetPixel( map, x, y+1 );
      }
    }
  }
}
#endif

void epsBox( unsigned char* map, unsigned x0, unsigned y0, unsigned x1, unsigned y1, int filled ) {
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

PI_THREAD(_epsSetup) {
  unsigned count;
  epsReset();

  addMessage( 1, "EP power on" );
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
  addMessage(2, "EP setting lookup tables" );
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

  epsSend(PARTIAL_DISPLAY_REFRESH,0);
  epsSend(0x00,1);

/* ShowMode() should be enough
  addMessage( 2, "EP wipe all");
  epClear();
*/
	state=1;
  return NULL;
}


void epsButton( unsigned key, void(*func)(void) ){
  if( state != 1 ) {
    addMessage( 0, "Cannot set button - %i!", state );
    return;
  }
	pinMode( key, INPUT );
	pullUpDnControl( key, PUD_UP ) ;
	wiringPiISR ( key, INT_EDGE_FALLING, func );
}

void epsPoweron( void ) {
  switch( state ){
    case 0:
      if( piThreadCreate(_epsSetup) != 0 ) {
        addMessage( 0, "Could not start EP init thread!" );
        return;
      }
    break;
    case 1:
      addMessage( 0, "EP display is already on!" );
    break;
    default:
      addMessage( 0, "EP Display is not initialized!" );
  }
}

void epsSetup( void ) {
  addMessage(1, "Initializing ePaper");
	if( state != -2 ) {
		fail( F_FAIL, "Display is already set up!");
	}
  state=-1;

	/* init wiringpi to use own pin layout */
	wiringPiSetup();

	/* init paper */
  pinMode( RST_PIN, OUTPUT);
  pinMode(  DC_PIN, OUTPUT);
  pinMode(  CS_PIN, OUTPUT);
	pinMode( BSY_PIN,  INPUT);

  if( wiringPiSPISetupMode(0, 32000000, 0) < 0 ) {
    addMessage( 0, "Could not set SPI mode!" );
    return;
  }
  state=0;
  epsPoweron();
}

/*
 * turn off the display in any case!
 */
void epsPoweroff(void) {
  if( state == 1 ) {
    addMessage(1, "Sending ePaper to sleep");
    epsIdle(); /* this may be a bad idea tho.. */
    epsSend( VCOM_AND_DATA_INTERVAL_SETTING, 0 );
    epsSend( 0xf7, 1 );
    epsSend( POWER_OFF, 0 );
    epsSend( DEEP_SLEEP, 0 );
    epsSend( 0xA5, 1 );
    state=0;
  }
}
