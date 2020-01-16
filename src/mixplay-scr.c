/* Screen saver client.
   Checks the player's state every ten seconds. If the player
	 is idle for longer than the given timeout, the screen is
	 powered off via DPMS */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

#include "config.h"
#include "mpclient.h"
#include "utils.h"

static int displayPower( int on ) {
	int dummy;
	int rv = -1;
	Display *dpy;
	char *disp=":0";

	dpy = XOpenDisplay(disp);    /*  Open display and check for success */
 	if (dpy == NULL) {
		fail(F_FAIL, "Unable to open display");
		return -1;
	}

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		DPMSEnable(dpy);
		usleep(100000);
		if (on) {
			DPMSForceLevel(dpy, DPMSModeOn);
			rv=1;
		}
		else  {
			DPMSForceLevel(dpy, DPMSModeOff);
			rv=0;
		}
	}
	else {
		fail(F_FAIL,"No DPMS support!");
	}
	XCloseDisplay(dpy);
	return rv;
}

/* returns 1 if the DPMSstate of the display is on */
static int getDisplayState() {
	int dummy;
	BOOL dummy2;
	int rv = -1;
	Display *dpy;
	char *disp=":0";
	CARD16 level;

	dpy = XOpenDisplay(disp);
	if (dpy == NULL) {
		fail(F_FAIL, "Unable to open display");
		return -1;
	}

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		DPMSEnable(dpy);
		DPMSInfo(dpy, &level, &dummy2);
		if( level == DPMSModeOn ) {
			rv=1;
		}
		else {
			rv=0;
		}
	}
	else {
		fail(F_FAIL,"No DPMS support!");
		rv=-1;
	}
	XCloseDisplay(dpy);
	return rv;
}

int main(){
	jsonObject *jo=NULL;
	time_t to=0;
	time_t timer=0;
	time_t now=0;
	mpcmd_t cmd=mpc_idle;
	int sstate;

	readConfig();

	/* could also be read from mixplayd */
	to=getConfig()->sleepto;
	if( to == 0 ) {
		fail(F_FAIL, "No timeout set, disabling screensaver!");
	}

	sstate=getDisplayState();
	if( sstate == -1 ) {
		fail(F_FAIL, "Cannot access DPMS!");
	}

	if( daemon( 1, 0 ) != 0 ) {
		fail( errno, "Could not demonize!" );
	}

	while ( cmd != mpc_quit ) {
		jo=getStatus(-1, MPCOMM_STAT);
		if( jsonPeek(jo, "type") == json_error ) {
			cmd=mpc_idle;
		}
		else {
			cmd=(mpcmd_t)jsonGetInt(jo,"status");
		}
		jsonDiscard(jo);
		now=time(0);

		/* nothing is playing */
		if( cmd == mpc_idle ) {
			/* is the screen physically on? */
			if( getDisplayState() == 1 ) {
				/* Screen is on and we think it should be off. So someone
					touched the screen. Give ten seconds to start anything,
					otherwise turn screen off again */
				if( sstate == 0 ) {
					timer=now-to+10;
					sstate=1;
				}
				/* screen is on */
				else {
					/* No timer yet, start countdown */
					if( timer == 0 ) {
						timer=now;
					}
					/* timeout hit? turn off screen */
					if( now - timer >= to ) {
						displayPower(0);
						sstate=0;
					}
				}
			} /* when the display is off, nothing needs to be done */
		}
		/* player state is not idle */
		else {
			/* we turned the screen off? Turn it on! */
			if( sstate == 0 ) {
				displayPower(1);
				sstate=1;
				timer=0;
			}
		}
		sleep(1);
	}
	displayPower(1);
}
