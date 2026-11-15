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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

#include "mphid.h"
#include "utils.h"
#include "config.h"
#include "player.h"
#include "mpclient.h"
#include "controller.h"

const mpcmd_t _mprccmds[MPRC_NUM] = {
	mpc_play,
	mpc_prev,
	mpc_next,
	mpc_fav | mpc_display,
	mpc_dnp | mpc_display,
	mpc_mute,
	mpc_quit,
	mpc_ivol,
	mpc_dvol
};

const char *_mprccmdstrings[MPRC_NUM] = {
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

/*
 * handles key events from the reserved HID device
 */
static void *_mpHID(void *arg) {
	int32_t code, i;
	int32_t fd = (int32_t) (long) arg;
	mpcmd_t cmd = mpc_idle;

	/* wait for the initialization to be done */
	while ((getConfig()->status != mpc_play) &&
		   (getConfig()->status != mpc_quit)) {
		sleep(1);				// poll every second
	}

	while (getConfig()->status != mpc_quit) {
		while (getEventCode(&code, fd, 250, 1) != 1);
		if (code > 0) {
			for (i = 0; i < MPRC_NUM; i++) {
				if (code == getConfig()->rccodes[i]) {
					cmd = _mprccmds[i];
				}
			}
		}
		/* special case for repeat keys */
		if (code < 0) {
			for (i = MPRC_SINGLE; i < MPRC_NUM; i++) {
				if (-code == getConfig()->rccodes[i]) {
					cmd = _mprccmds[i];
				}
			}
		}

		if (cmd != mpc_idle) {
			addMessage(2, "HID: %s", mpcString(cmd));
			setCommand(cmd, NULL, 0);
			cmd = mpc_idle;
		}
	}
	return NULL;
}

pthread_t startHID(int32_t fd) {
	pthread_t tid;

	if (pthread_create(&tid, NULL, _mpHID, (void *) (long) fd) != 0) {
		addMessage(0, "Could not create mpHID thread!");
		return -1;
	}
	pthread_setname_np(tid, "mpHID");
	return tid;
}

/**
 * helperfunction for scandir()
 */
static int32_t devsel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') && endsWith(entry->d_name, "kbd"));
}

/**
 * initialize HID configuration
 * 
 * this also initializes a 
 */
int32_t HIDinit() {
	int32_t numdev = 0;
	int32_t c = -1;
	int32_t fd = -1;
	int32_t i;
	struct dirent **devices = NULL;

	numdev = scandir("/dev/input/by-id/", &devices, devsel, alphasort);
	if (numdev < 1) {
		printf("No input devices available!\n");
		return -1;
	}

	printf("Select input device:\n");
	for (i = 0; i < numdev; i++) {
		printf("%2i - %s\n", i, devices[i]->d_name);
	}
	printf(" x - cancel\n");
	while (c == -1) {
		c = getch(1000);
		if (c == 'x') {
			printf("\nExit.\n");
			return 0;
		}
		else {
			c = c - '0';
			if ((c < 0) || (c > numdev)) {
				c = -1;
			}
		}
	}

	printf("\rOkay, trying to init %s\n", devices[c]->d_name);

	getConfig()->rcdev = strdup(devices[c]->d_name);
	fd = initHID();
	if (fd == -1) {
		printf("Failed to use %s!\n", devices[c]->d_name);
		return -1;
	}

	printf("\nStarting training. Press the button to use for the printed\n");
	printf("command within three seconds, otherwise the command will be\n");
	printf("unassigned.\n\n");

	do {
		printf("Press any key to start..\n");
		while (getch(1000) == -1);

		for (i = 0; i < MPRC_NUM; i++) {
			/* skip mpc_quit */
			if (_mprccmds[i] == mpc_quit) {
				getConfig()->rccodes[i] = -1;
				continue;
			}
			printf("Code for %s: ", _mprccmdstrings[i]);
			fflush(stdout);
			if (getEventCode(&c, fd, 3000, 0) == -1) {
				printf("NONE\n");
				getConfig()->rccodes[i] = -1;
			}
			else {
				printf("%i\n", c);
				getConfig()->rccodes[i] = c;
				sleep(1);		// give the user a chance to lift the finger again
			}
		}
		printf("Okay? (y)\n");
		while ((c = getch(1000)) == -1);
	} while (c != 'y');

	writeConfig(NULL);

	return 0;
}

/*
 * check for a HID device and try to reserve it.
 */
int32_t initHID() {
	int32_t fd = -1;
	char device[MAXPATHLEN];

	if (getConfig()->rcdev == NULL) {
		addMessage(1, "No input device set!");
	}
	else {
		snprintf(device, MAXPATHLEN, "/dev/input/by-id/%s",
				 getConfig()->rcdev);
		/* check for proper HID entry */
		fd = open(device, O_RDWR | O_NONBLOCK, S_IRUSR | S_IWUSR);
		if (fd != -1) {
			/* try to grab all events */
			if (ioctl(fd, EVIOCGRAB, 1) != 0) {
				addMessage(0, "Could not grab HID events! (%s)",
						   strerror(errno));
				close(fd);
				return -1;
			}
		}
		else {
			if (errno == EACCES) {
				addMessage(0,
						   "Could not access device, user needs to be in the 'input' group!");
			}
			else {
				addMessage(0, "No HID device %s (%s)", getConfig()->rcdev,
						   strerror(errno));
			}
		}
	}

	return fd;
}

mpcmd_t hidCMD(int32_t c) {
	const char keys[MPRC_NUM + 1] = " pnfd-Q.,";
	int32_t i;
	mpcmd_t cmd = mpc_idle;

	for (i = 0; i < MPRC_NUM; i++) {
		if (c == keys[i]) {
			cmd = _mprccmds[i];
		}
	}

	if (c == '\n') {
		hidPrintline("%s", "");
	}

	if (c == '?') {
		hidPrintline("Commands:\n"
					 "space - play/pause\n"
					 " p - previous title\n"
					 " n - next title\n"
					 " f - mark as favourite\n"
					 " d - mark as DNP\n"
					 " - - mute\n"
					 " Q - Quit\n"
					 " . - increase volume\n" " , - decrease volume");
	}
	return cmd;
}

void hidPrintline(const char *text, ...) {
	va_list args;

	printf("\r");
	va_start(args, text);
	vprintf(text, args);
	va_end(args);
	printf("\nMP> ");
	fflush(stdout);
}
