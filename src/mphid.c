#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
#include "mphid.h"
#include "utils.h"
#include "config.h"
#include "player.h"

static char _lasttitle[MAXPATHLEN+1];
static mpcmd_t _last=mpc_start;

static void printline( const char* text, ... ){
	va_list args;
	printf( "\r" );
	va_start( args, text );
	vprintf( text, args );
	va_end( args );
	printf( "\nMP> " );
	fflush( stdout );
}

/*
 * print title and play changes
 */
void hidUpdateHook() {
	char *title=NULL;

	if( ( getConfig()->current!=NULL ) &&
	( getConfig()->current->title != NULL ) ) {
		title=getConfig()->current->title->display;
	}

	/* has the title changed? */
	if ( ( title != NULL ) && ( strcmp( title, _lasttitle ) != 0 ) ) {
		strtcpy( _lasttitle, title, MAXPATHLEN );
		printline("Now playing: %s",title);
	}

	/* has the status changed? */
	if( getConfig()->status != _last ) {
		_last=getConfig()->status;
		switch(_last) {
			case mpc_idle:
				printline("[PAUSE]");
				break;
			case mpc_play:
				printline("[PLAY]");
				break;
			default:
				/* ignored */
				break;
		}
	}
}

/* the most simple HID implementation
   The hard coded keypresses should be configurable so that an existing
	 flirc setting can just be learned, or that mixplay can learn about
	 an existing remote. */
void runHID(void) {
	int c;
	mpconfig_t *config=getConfig();
	char *pass;
	pass=falloc(8,1);
	strcpy(pass,"mixplay");

	/* wait for the initialization to be done */
	while( ( config->status != mpc_play ) &&
	       ( config->status != mpc_quit ) ){
		sleep(1);
	}

	while( config->status != mpc_quit ) {
		c=getch(750);
		switch(c){
			case 'p':
				setCommand( mpc_prev, NULL );
				break;
			case 'n':
				setCommand( mpc_next, NULL );
				break;
			case ' ':
				setCommand( mpc_play, NULL );
				break;
			case 'f':
				setCommand( mpc_fav, NULL );
				break;
			case 'd':
				setCommand( mpc_dnp, NULL );
				break;
			case 'Q':
				printline("[QUIT]");
				setCommand( mpc_quit, pass );
				break;
			case -1:
				/* timeout - ignore */
				break;
			default:
				printf("\rCommands:\n");
				printf(" [space] - play/pause\n");
				printf("    p    - previous\n");
				printf("    n    - next\n");
				printf("    f    - favourite\n");
				printf("    d    - do not play\n");
				printf("    Q    - quit\n");
				printline("");
				break;
		}
	}
}
