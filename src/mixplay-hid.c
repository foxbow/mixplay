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

static void drawAll(int fd) {
	jsonObject *jo=NULL;
	char *title=NULL;
	char *artist;
	int rv;

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
		if(( title == NULL ) || ( artist == NULL )) {
			fail(F_FAIL, "Invalid status from server!");
		}
	}

	printf( "%s - %s\r", artist, title );
	fflush(stdout);

	jsonDiscard(jo);
}

int main( ){
	char c=0;
	mpcmd_t cmd=mpc_idle;
	int running=1;
	int fd=0;

	if( readConfig() == NULL ) {
		fail( F_FAIL, "No mixplayd configuration found!");
	}

	fd = getConnection();
	if( fd == -1 ) {
		fail(errno, "Could not connect to server!");
	}

	printf( "Mixplay HID demo\n");

	while ( running ) {
		c=getch(1000);
		cmd=mpc_idle;
		switch(c) {
			case ' ':
				cmd=mpc_play;
				break;
			case 'n':
			case 's':
				cmd=mpc_next;
				break;
			case 'p':
				cmd=mpc_prev;
				break;
			case 'd':
				cmd=mpc_dnp;
				break;
			case 'f':
				cmd=mpc_fav;
				break;
			case 'q':
				running=0;
				break;
		}
		if( sendCMD( fd, cmd ) < 0 ) {
			fail(errno, "Command %s failed.", mpcString(cmd) );
			close(fd);
		}
		if( cmd != mpc_idle ) {
			printf( "\nSent %s\n", mpcString(cmd) );
		}
		drawAll(fd);
	}
	close(fd);
}
