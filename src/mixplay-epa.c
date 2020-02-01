/* EPA client for mixplayd */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"
#include "mpclient.h"
#include "utils.h"
#include "epasupp.h"
#include "mpepa.h"

int main( int argc, char **argv ){
	jsonObject *jo=NULL;
	time_t to=0;
	time_t timer=0;
	mpcmd_t cmd=mpc_idle;
	int fd=0;
	int sstate=1;
	mptitle_t *title=NULL;
	char last[MAXPATHLEN+1]="";

	readConfig();
	fd = getConnection(NULL);
	if( fd < 0 ) {
		fail(errno, "Could not connect to server!");
	}

	epSetup();

	if( daemon( 1, 0 ) != 0 ) {
		fail( errno, "Could not demonize!" );
	}

	while ( cmd != mpc_quit ) {
		jo=getStatus(fd, MPCOMM_FULLSTAT);
		if( jsonPeek(jo, "type") == json_error ) {
			cmd=mpc_quit;
		}
		else {
			cmd=(mpcmd_t)jsonGetInt(jo, "status");
		}

		if( cmd == mpc_quit ) {
			break;
		}

		jsonGetTitle(jo, "current", &title);
		if( strcmp( last, title->display ) != 0 ) {
			strcpy(last, title->display);
			epsWipeFull( epm_both );

			if( cmd == mpc_play ) {
				if( title->flags & MP_FAV ) {
					epsDrawSymbol( epm_red, 5, 150, ep_fav );
				}
				else {
					epsDrawSymbol( epm_red, 5, 150, ep_play );
				}
				epsDrawString( epm_black, 40, 120, title->artist, 1 );
				epsDrawString( epm_black, 40, 75,  title->title, 2 );
				epsDrawString( epm_black, 40, 40,  title->album, 0 );
			}
			else if( cmd == mpc_idle ){
				epsDrawSymbol( epm_red, 5, 150, ep_pause );
				epsDrawString( epm_red, 40, 120, title->artist, 1 );
				epsDrawString( epm_red, 40, 75,  title->title, 2 );
				epsDrawString( epm_red, 40, 40,  title->album, 0 );
			}
			else {
				epsDrawString( epm_red, 40, 50, "Initializing", 1 );
			}

			jsonGetTitle(jo, "prev.0", &title);
			epsDrawString( epm_red, 40, 160, "---", 1 );

			jsonGetTitle(jo, "next.0", &title);
			epsDrawString( epm_red, 40, 1, title->display, 0 );

			epsLine( epm_black, 0, 128, 30, 128 );
			epsLine( epm_black, 0, 66, 30, 66 );
			epsLine( epm_black, 30, 0, 30, Y_MAX );
			epsLine( epm_black, 30, 20, X_MAX, 20 );
			epsLine( epm_black, 30, 150, X_MAX, 150 );

			if( jsonGetInt(jo, "mpmode") & PM_DATABASE ) {
				epsDrawSymbol( epm_black, 5, 90, ep_next );
				epsDrawSymbol( epm_black, 5, 30, ep_dnp );
			}
			else {
				epsDrawSymbol( epm_black, 5, 90, ep_up );
				epsDrawSymbol( epm_black, 5, 30, ep_down );
			}

			epsDisplay( );
		}

		jsonDiscard(jo);
		sleep(1);
	}

	epExit( void );
	close(fd);
}
