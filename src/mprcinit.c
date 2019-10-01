#include <stdio.h>

#include "config.h"
#include "mphid.h"
#include "utils.h"

int main( int argc, char **argv ) {
	mpconfig_t *config=readConfig( );
	control=readConfig( );
	if( control == NULL ) {
		control=createConfig();
		if( control == NULL ) {
			printf( "Could not create config file!\n" );
			return 1;
		}
	}


}
