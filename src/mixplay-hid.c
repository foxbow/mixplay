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
	char *title=NULL;
	char *artist;
	char current[MAXPATHLEN];
	int rv;
	int state;

	jo=getStatus(fd,1);
	title=jsonGetError(jo);
	if(title!=NULL) {
		fail(F_FAIL, "JSON Error! %s", title);
	}

	if( jsonPeek(jo, "type") == json_error ) {
		rv=jsonGetInt(jo, "error");
		fail(F_FAIL, "Server returned %i!", rv );
	}
	else {
		title=jsonGetStr(jo,"current.title");
		artist=jsonGetStr(jo,"current.artist");
		state=jsonGetInt(jo,"status");
		if(( title == NULL ) || ( artist == NULL )) {
			fail(F_FAIL, "Invalid status from server!");
		}
	}

	snprintf(current, MAXPATHLEN-1, "[%s] %s - %s",
			(state==0)?">":"|", artist, title );
	if( strcmp(current, last) != 0 ) {
		strcpy(last, current);
		hidPrintline( "%s\r", current );
		fflush(stdout);
	}

	jsonDiscard(jo);
}

int main( ){
	char c=0;
	int running=1;
	int fd=0;
	mpcmd_t cmd=mpc_idle;

	if( readConfig() == NULL ) {
		fail( F_FAIL, "No mixplayd configuration found!");
	}

	fd = getConnection();
	if( fd == -1 ) {
		fail(errno, "Could not connect to server!");
	}

	hidPrintline( "Mixplay HID demo\n");

	while ( running ) {
		c=getch(1000);
		cmd=hidCMD(c);
		if( cmd == mpc_quit ) {
			running = 0;
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
