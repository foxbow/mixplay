/*
 * HID control for mixplayd.
 * this takes a kbd input device and interprets the events coming from there
 * additionally this holds the debug keyboard interface that uses the
 * common stdin channel.
 *
 * The HID configuration consists of a device name and
 * an event mapping that needs to be created with the mphidtrain utility first.
 */
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "mphid.h"
#include "utils.h"
#include "config.h"
#include "player.h"
#include "mpclient.h"

const mpcmd_t _mprccmds[MPRC_NUM]={
	mpc_play,
	mpc_prev,
	mpc_next,
	mpc_fav|mpc_display,
	mpc_dnp|mpc_display,
	mpc_mute,
	mpc_quit,
	mpc_ivol,
	mpc_dvol
};

const char *_mprccmdstrings[MPRC_NUM]={
	"Play/pause",
	"Previous title",
	"Next title",
	"Mark as favourite",
	"Mark as Do Not Play",
	"Mute volume",
	"Quit",
	"Increase volume",
	"Decrease volume"
};

mpcmd_t hidCMD( int c ) {
	const char keys[MPRC_NUM+1]=" pnfd-Q.,";
	int i;
	mpcmd_t cmd=mpc_idle;

	for( i=0; i<MPRC_NUM; i++ ) {
		if( c == keys[i] ) {
			cmd=_mprccmds[i];
		}
	}

	if( c == '\n' ) {
		hidPrintline("%s", "");
	}

	return cmd;
}

void hidPrintline( const char* text, ... ){
	va_list args;
	printf( "\r" );
	va_start( args, text );
	vprintf( text, args );
	va_end( args );
	printf( "\nMP> " );
	fflush( stdout );
}
