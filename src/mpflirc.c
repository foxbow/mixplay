#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <stdio.h>
#include <errno.h>

#include "config.h"
#include "controller.h"
/*
 * handles key events from the reserved HID device
 */
static void *_mpFLIRC(void *arg) {
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
			setCommand(cmd, NULL);
			cmd = mpc_idle;
		}
	}
	return NULL;
}

pthread_t startFLIRC(int32_t fd) {
	pthread_t tid;

	if (pthread_create(&tid, NULL, _mpFLIRC, (void *) (long) fd) != 0) {
		addMessage(0, "Could not create mpHID thread!");
		return -1;
	}
	return tid;
}

/*
 * check for a HID device and try to reserve it.
 */
int32_t initFLIRC() {
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
