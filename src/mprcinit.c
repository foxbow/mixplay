#include <stdio.h>

#include "config.h"
#include "mphid.h"
#include "utils.h"

/**
 * helperfunction for scandir()
 */
static int devsel( const struct dirent *entry ) {
	return( ( entry->d_name[0] != '.' ) &&
			endsWith( entry->d_name, "kbd" ) );
}

int main( int argc, char **argv ) {
	int numdev=0;
	int c=-1;
	int fd=-1;
	mpconfig_t *config=NULL;
	struct dirent **devices;

	numdev=scandir( "/dev/input/by-id/", &devices, devsel, alphasort );
	if( numdev < 1 ) {
		printf("No input devices available!\n");
		return -1;
	}

	config=readConfig( );
	control=readConfig( );
	if( control == NULL ) {
		control=createConfig();
		if( control == NULL ) {
			printf( "Could not create config file!\n" );
			return 1;
		}
	}

  printf("Select input device:\n");
	for( i=0; i<numdev; i++ ) {
		printf("%2i - %s\n", devices[i]->d_name );
	}
	printf(" x - cancel\n" );
	printf("-> "); fflush( stdout );
	while( c == -1 ) {
		c=getchar( 1000 );
		if( c == 'x' ) {
			c=numdev+1;
		}
		else {
			c=c-'0';
			if( ( c < 0 ) || ( c > numdev ) {
				c=-1;
			}
		}
	}

	getConfig()->rcdev=strdup(devices[c]->d_name);
	fd=initHID();
	if( fd == -1 ) {
		printf("Failed to use %s!\n", devices[c]->d_name );
		return -1;
	}

}
