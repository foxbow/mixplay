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
	}
	XCloseDisplay(dpy);
	return rv;
}

int main( int argc, char **argv ){
	jsonObject *jo=NULL;
	time_t to=0;
	time_t timer=0;
	mpcmd_t cmd=mpc_idle;
	int fd=0;
	int sstate=1;

	if( argc != 2 ) {
		fail(F_FAIL, "No timeout given!");
	}

	to=atoi(argv[1]);

	if( to < 10 ) {
		fail(F_FAIL, "Timeout needs to be larger than 10 seconds!");
	}

	readConfig();
	fd = getConnection();
	if( fd < 0 ) {
		fail(errno, "Could not connect to server!");
	}

	if( daemon( 1, 0 ) != 0 ) {
		fail( errno, "Could not demonize!" );
	}

	/* allow everything to start up */
	sleep(10);
	while ( cmd != mpc_quit ) {
		jo=getStatus(fd,0);
		if( jsonPeek(jo, "type") == json_error ) {
			cmd=mpc_quit;
		}
		else {
			cmd=(mpcmd_t)jsonGetInt(jo,"status");
		}
		jsonDiscard(jo);

		if( cmd == mpc_idle ) {
			/* screen is on even though we turned it off, so someone else woke
			   it up, better reset the timeout */
			if(( sstate == 0 ) && (getDisplayState() == 1)) {
				timer=0;
				sstate=1;
			}
			if(timer == 0) {
				timer=time(0);
			}
			else if( time(0) - timer > to ) {
				displayPower(0);
				sstate=0;
			}
			sleep(5);
		}
		else {
			if( timer > 0 ) {
				displayPower(1);
				sstate=1;
			}
			timer=0;
			sleep(1);
		}
	}
	close(fd);
}
