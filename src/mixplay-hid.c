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

char last[MAXPATHLEN]="";

static void drawAll(int fd) {
	jsonObject *jo=NULL;
	char *text=NULL;
	mptitle_t title;
	char current[MAXPATHLEN];
	int rv;
	int state;

	jo=getStatus(fd, MPCOMM_FULLSTAT);
	text=jsonGetError(jo);
	if(text != NULL) {
		fail(F_FAIL, "JSON Error! %s", text);
	}

	if(jsonPeek(jo, "type") == json_error) {
		rv=jsonGetInt(jo, "error");
		fail(F_FAIL, "Server returned %i!", rv);
	}
	else {
		jsonGetTitle(jo, "current", &title);
		state=jsonGetInt(jo,"status");
	}
	jsonDiscard(jo);

	snprintf(current, MAXPATHLEN-1, "[%s] %s",
			(state == mpc_play)?">":"|", title.display );
	if( strcmp(current, last) != 0 ) {
		strcpy(last, current);
		hidPrintline( "%s\r", current );
		fflush(stdout);
	}
}

int main( int argc, char **argv ){
	char c=0;
	int running=1;
	int fd=0;
	mpcmd_t cmd=mpc_idle;

	if( readConfig() == NULL ) {
		fail( F_FAIL, "No mixplayd configuration found!");
	}

	if( argc == 2 ) {
		fd = getConnection(argv[1]);
		if( fd == -1 ) {
			fail(errno, "Could not connect to server at %s!", argv[1]);
		}
	}
	else {
		fd = getConnection(NULL);
		if( fd == -1 ) {
			fail(errno, "Could not connect to local server!");
		}
	}
	hidPrintline( "Mixplay HID demo\n");

	while ( running ) {
		c=getch(1000);
		cmd=hidCMD(c);
		if( cmd == mpc_quit ) {
			running = 0;
			break;
		}
		else if( cmd != mpc_idle ) {
			hidPrintline("Sent: %s", mpcString(cmd)+4);
			sendCMD(fd, cmd);
		}
		drawAll(fd);
	}
	puts("");
	close(fd);
}
