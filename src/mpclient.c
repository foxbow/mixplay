/* simple API to send commands to mixplayd */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "mpclient.h"
#include "utils.h"

/* open a connection to the server.
   returns:
	 -1 : No socket available
	 -2 : unable to connect to server
	 on error and the socket on success.
*/
int getConnection() {
	struct sockaddr_in server;
	int fd;

	fd=socket(AF_INET, SOCK_STREAM, 0);
	if( fd == -1 ) {
		return -1;
	}

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = inet_addr("127.0.0.1");
	server.sin_port = htons( getConfig()->port );
	if( connect(fd, (struct sockaddr*)&server, sizeof(server)) == -1 ) {
		close(fd);
		return -2;
	}
	return fd;
}

/* send the command to mixplayd.
   returns:
	  1 - success
	  0 - no command to send
	 -1 - failure on send
	 -2 - error return from server  */
int sendCMD(int fd, mpcmd_t cmd){
	char line[1024];
	if( cmd == mpc_idle ) {
		return 0;
	}
	snprintf( line, 1023, "get /cmd/%i x\015\012", cmd );
	if( send( fd, line, strlen(line), 0 ) == -1 ) {
		return -1;
	}
	while( recv( fd, line, 1024, 0 ) == 1024 );
	if( strstr( line, "204" ) == NULL ) {
		printf("Reply: %s\n", line);
		return -2;
	}
	return 1;
}

int getCurrentTitle( char *title, int tlen ) {
	char line[1024];
	int fd;
	int rlen=0;
	char *pos=NULL;

	fd=getConnection();
	if( fd < 0 ) {
		return -1;
	}

	if( send( fd, "get /title/info \015\012", 18, 0 ) == -1 ) {
		close(fd);
		return -2;
	}
	while( recv( fd, line, 1024, 0 ) == 1024 );
	close(fd);

	pos=strstr( line, "Content-Length:" );
	if( pos == NULL ) {
		return -3;
	}
  pos += 16;
	rlen=atoi(pos);

	if( rlen == 0 ) {
		return -4;
	}
	/* add terminating NUL */
	rlen=rlen+1;

	pos = strstr(pos, "\015\012\015\012" );
	if( pos == NULL ) {
		return -5;
	}
	pos+=4;

	strtcpy( title, pos, tlen < rlen ? tlen : rlen );
	return strlen(title);
}
