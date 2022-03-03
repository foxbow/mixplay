/* HID client for mixplayd.
   just an example for the mpclient API */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"
#include "mpclient.h"
#include "utils.h"
#include "mphid.h"

char last[MAXPATHLEN + 1] = "";

static int32_t drawAll() {
	jsonObject *jo = NULL;
	char *text = NULL;
	mptitle_t title;
	char current[MAXPATHLEN + 5];
	int32_t state = 0;

	do {
		jo = getStatus(MPCOMM_TITLES);
	} while (jo == NULL);

	text = jsonGetError(jo);
	if (text != NULL) {
		fail(F_FAIL, "JSON Error! %s", text);
	}
	if (jsonPeek(jo, "error") != json_error) {
		state = jsonGetInt(jo, "error");
		switch (state) {
		case -1:				/* error during send/receive */
			fail(F_FAIL, "Server did not reply!");
			break;
		case 204:				/* empty HTTP reply - ignore */
			break;
		default:				/* should not happen, so report it */
			hidPrintline("Server returned code %i", state);
		}
	}
	if (jsonPeek(jo, "type") != json_error) {
		state = jsonGetInt(jo, "status");
		if (jsonGetInt(jo, "type") & MPCOMM_TITLES) {
			jsonGetTitle(jo, "current", &title);
			snprintf(current, MAXPATHLEN, "%s", title.display);
			if (strcmp(current, last) != 0) {
				strcpy(last, current);
				hidPrintline("%s\r", current);
				fflush(stdout);
			}
		}
	}
	jsonDiscard(jo);

	return state;
}

int32_t main(int32_t argc, char **argv) {
	char c = 0;
	int32_t running = 1;
	mpcmd_t cmd = mpc_idle;

	if (readConfig() == NULL) {
		fail(F_FAIL, "No mixplayd configuration found!");
	}

	if (argc == 2) {
		setMPHost(argv[1]);
	}

	if (getCurrentTitle(last, MAXPATHLEN) < 0) {
		fail(errno, "Could not get current title!");
	}

	hidPrintline("Mixplay HID demo\n");
	hidPrintline("%s", last);

	while (running) {
		c = getch(1000);
		cmd = hidCMD(c);
		if (cmd == mpc_quit) {
			running = 0;
			break;
		}
		else if (cmd != mpc_idle) {
			hidPrintline("Sent: %s", mpcString(cmd));
			sendCMD(cmd);
		}
		drawAll();
	}
	puts("");
}
