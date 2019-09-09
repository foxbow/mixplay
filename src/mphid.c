#include <unistd.h>
#include "mphid.h"
#include "utils.h"
#include "config.h"
#include "player.h"

/* the most simple HID implementation
   The hard coded keypresses should be configurable so that an existing
	 flirc setting can just be learned, or that mixplay can learn about
	 an existing remote. */
void runHID(void) {
	int c;
	mpconfig_t *config=getConfig();

	while( config->status != mpc_play ) {
		addMessage(0,"Waiting..");
		sleep(1);
	}

	addMessage(0,"Starting HID");

	while( config->status != mpc_quit ) {
		c=getch(750);
		switch(c){
			case 'p':
				setCommand( mpc_prev );
				break;
			case 'n':
				setCommand( mpc_next );
				break;
			case ' ':
				setCommand( mpc_play );
				break;
			case 'f':
				setCommand( mpc_fav );
				break;
			case 'd':
				setCommand( mpc_dnp );
				break;
			case 'Q':
				/* quit is tricky as the mpc_quit commmand needs a password, so we
				   emulate a CTRL-C while we should probably take the secure route
					 instead to make sure we don't disturb some async command */
				config->command=mpc_quit;
				config->status=mpc_quit;
				break;
			case -1:
				/* timeout - ignore */
				break;
			default:
				addMessage(0, "Unknown command %c", c );
		}
	}
}
