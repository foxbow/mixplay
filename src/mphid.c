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
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stropts.h>
#include <linux/input.h>
#include <errno.h>
#include <string.h>

#include "mphid.h"
#include "utils.h"
#include "config.h"
#include "player.h"

const mpcmd_t _mprccmds[MPRC_NUM]={
	mpc_play,
	mpc_prev,
	mpc_next,
	mpc_fav,
	mpc_dnp,
	mpc_mute,
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
	"Increase volume",
	"Decrease volume"
};

/*
 * check for a HID device and try to reserve it.
 */
int initHID() {
	int fd=-1;
	char device[MAXPATHLEN];

	if( getConfig()->rcdev == NULL ) {
		addMessage( 1, "No input device set!\n" );
	}
	else {
		snprintf( device, MAXPATHLEN, "/dev/input/by-id/%s", getConfig()->rcdev );
		/* check for proper HID entry */
		fd=open( device, O_RDWR|O_NONBLOCK, S_IRUSR|S_IWUSR );
		if( fd != -1 ) {
			/* try to grab all events */
			if( ioctl( fd, EVIOCGRAB, 1 ) != 0 ) {
				addMessage( 0, "Could not grab HID events! (%s)", strerror(errno));
				close(fd);
				return -1;
			}
		}
		else {
			if( errno == EACCES ) {
				addMessage( 0, "Could not access device, user needs to be in the 'input' group!" );
			}
			else {
				addMessage( 0, "No HID device %s (%s)", getConfig()->rcdev, strerror(errno));
			}
		}
	}

	return fd;
}

/*
 * handles key events from the reserved HID device
 */
static void *_mpHID( void *arg ) {
	int code, i;
	int fd = (int)(long)arg;
	mpcmd_t cmd=mpc_idle;

	/* wait for the initialization to be done */
	while( ( getConfig()->status != mpc_play ) &&
	       ( getConfig()->status != mpc_quit ) ){
		sleep(1);
	}

	while( getConfig()->status != mpc_quit ) {
		while( getEventCode( &code, fd, 250, 1 ) != 1);
		if( code > 0 ) {
			for( i=0; i<MPRC_NUM; i++ ) {
				if( code == getConfig()->rccodes[i] ) {
					cmd=_mprccmds[i];
				}
			}
		}
		/* special case for repeat keys */
		if( code < 0 ) {
			for( i=MPRC_SINGLE; i<MPRC_NUM; i++ ) {
				if( -code == getConfig()->rccodes[i] ) {
					cmd=_mprccmds[i];
				}
			}
		}

		if( cmd != mpc_idle ) {
			addMessage( 2, "HID: %s", mpcString(cmd) );
			setCommand( cmd, NULL );
			cmd=mpc_idle;
		}
	}
	return NULL;
}

pthread_t startHID( int fd ) {
	pthread_t tid;
	if( pthread_create( &tid, NULL, _mpHID, (void *)(long)fd ) != 0 ) {
		addMessage( 0, "Could not create mpHID thread!" );
		return -1;
	}
	return tid;
}
