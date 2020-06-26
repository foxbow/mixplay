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

char last[MAXPATHLEN+1]="";

static int drawAll(clientInfo *ci) {
	jsonObject *jo=NULL;
	char *text=NULL;
	mptitle_t title;
	char current[MAXPATHLEN+5];
	int rv;
	int state;

	jo=getStatus(ci, MPCOMM_STAT);
	text=jsonGetError(jo);
	if(text != NULL) {
		fail(F_FAIL, "JSON Error! %s", text);
	}

	if(jsonPeek(jo, "type") == json_error) {
		rv=jsonGetInt(jo, "error");
		fail(F_FAIL, "Server returned %i!", rv);
	}
	else {
		state=jsonGetInt(jo,"status");
		if(jsonGetInt(jo, "type") & MPCOMM_TITLES) {
			jsonGetTitle(jo, "current", &title);
			snprintf(current, MAXPATHLEN, "%s", title.display );
			if( strcmp(current, last) != 0 ) {
				strcpy(last, current);
				hidPrintline( "%s\r", current );
				fflush(stdout);
			}
		}
	}
	jsonDiscard(jo);

	return state;
}

int main( int argc, char **argv ){
	char c=0;
	int running=1;
	int fd=0;
	clientInfo *ci;
	mpcmd_t cmd=mpc_idle;

	if( readConfig() == NULL ) {
		fail( F_FAIL, "No mixplayd configuration found!");
	}

	fd=getCurrentTitle(last, MAXPATHLEN);
	if( fd < 0 ) {
		fail( errno, "Could not get current title!");
	}

	if( argc == 2 ) {
		setMPHost(argv[1]);
	}
	else {
		ci = getConnection(1);
		if( ci->fd < 0 ) {
			fail(errno, "Could not connect to server (%i)!", ci->fd);
		}
	}
	hidPrintline( "Mixplay HID demo\n");
	hidPrintline( "%s", last);

	while ( running ) {
		c=getch(1000);
		cmd=hidCMD(c);
		if( cmd == mpc_quit ) {
			running = 0;
			break;
		}
		else if( cmd != mpc_idle ) {
			hidPrintline("Sent: %s", mpcString(cmd)+4);
			sendCMD(ci, cmd);
		}
		drawAll(ci);
	}
	free(ci);
	puts("");
	close(fd);
}
