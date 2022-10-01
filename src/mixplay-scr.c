/* Screen saver client.
   Checks the player's state every second. If the player
	 is idle for longer than the given timeout, the screen is
	 powered off via DPMS */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

#include "config.h"
#include "mpclient.h"
#include "utils.h"

static int32_t displayPower(int32_t on) {
	int32_t dummy;
	int32_t rv = -1;
	Display *dpy;
	char *disp = ":0";

	dpy = XOpenDisplay(disp);	/*  Open display and check for success */
	if (dpy == NULL) {
		return -1;
	}

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		DPMSEnable(dpy);
		usleep(100000);
		if (on) {
			DPMSForceLevel(dpy, DPMSModeOn);
			rv = 1;
		}
		else {
			DPMSForceLevel(dpy, DPMSModeOff);
			rv = 0;
		}
	}
	else {
		rv = -2;
	}
	XCloseDisplay(dpy);
	return rv;
}

/* returns 1 if the DPMSstate of the display is on */
static int32_t getDisplayState() {
	int32_t dummy;
	BOOL dummy2;
	int32_t rv = -1;
	Display *dpy;
	char *disp = ":0";
	CARD16 level;

	dpy = XOpenDisplay(disp);
	if (dpy == NULL) {
		return -1;
	}

	if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
		DPMSEnable(dpy);
		DPMSInfo(dpy, &level, &dummy2);
		if (level == DPMSModeOn) {
			rv = 1;
		}
		else {
			rv = 0;
		}
	}
	else {
		rv = -2;
	}
	XCloseDisplay(dpy);
	return rv;
}

int32_t main() {
	jsonObject *jo = NULL;
	int32_t to = -6;
	int32_t timer = 0;
	mpcmd_t state = mpc_idle;
	int32_t sstate;
	int32_t dstate;

	if (daemon(1, 0) != 0) {
		fail(errno, "Could not demonize!");
	}

	/* give X11 some time to start up */
	sleep(10);

	dstate = getDisplayState();
	sstate = dstate;
	if (sstate < 0) {
		/* this will get lost since we're already demonized */
		fail(F_FAIL, "Cannot access DPMS!");
	}


	/* todo parameters: timeout, host, port */
	while (to < 0) {
		sleep(5);
		jo = getStatus(MPCOMM_CONFIG);
		if (jsonPeek(jo, "type") == json_error) {
			to++;
		}
		else {
			to = jsonGetInt(jo, "sleepto");
		}
		jsonDiscard(jo);
	}

	if (to == 0) {
		fail(F_FAIL, "No timeout set, disabling screensaver!");
	}
	timer = to;

	while (state != (mpc_idle + 1)) {
		jo = getStatus(MPCOMM_STAT);
		if (jsonPeek(jo, "type") == json_error) {
			state = mpc_idle;
		}
		else {
			state = (mpcmd_t) jsonGetInt(jo, "status");
		}
		jsonDiscard(jo);

		/* get display state */
		dstate = getDisplayState();
		/* nothing is playing */
		if (state == mpc_idle) {
			/* is the screen physically on? */
			if (dstate == 1) {
				/* should it be off - then turn it off again in 10s */
				if (sstate == 0) {
					timer = 10;
					sstate = 1;
				}
				/* screen is on */
				else {
					/* No timer yet, start countdown */
					if (timer == 0) {
						timer = to;
						sstate = 1;
					}
					else {
						timer--;
					}
					/* timeout hit? turn off screen */
					if (timer <= 0) {
						displayPower(0);
						sstate = 0;
					}
				}
			}					/* when the display is off, nothing needs to be done */
		}
		/* player state is not idle */
		else {
			/* we turned the screen off? Turn it on! */
			if ((sstate == 0) || (dstate == 0)) {
				displayPower(1);
				sstate = 1;
			}
			timer = to;
		}
		sleep(1);
	}
	/* always turn the screen on on exit */
	displayPower(1);
}
