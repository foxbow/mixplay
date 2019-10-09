#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>

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

/*
 * Print errormessage and exit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail( const int error, const char* msg, ... ) {
	va_list args;
	fprintf( stdout, "\n" );
	printf("mprcinit: ");
	va_start( args, msg );
	vfprintf( stdout, msg, args );
	va_end( args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
	}
	exit( error );
}

int main(  ) {
	int numdev=0;
	int c=-1;
	int fd=-1;
	int i;
	mpconfig_t *config=NULL;
	struct dirent **devices;

	numdev=scandir( "/dev/input/by-id/", &devices, devsel, alphasort );
	if( numdev < 1 ) {
		printf("No input devices available!\n");
		return -1;
	}

	config=readConfig( );
	if( config == NULL ) {
		config=createConfig();
		if( config == NULL ) {
			printf( "Could not create config file!\n" );
			return -1;
		}
	}

  printf("Select input device:\n");
	for( i=0; i<numdev; i++ ) {
		printf("%2i - %s\n", i, devices[i]->d_name );
	}
	printf(" x - cancel\n" );
	while( c == -1 ) {
		c=getch( 1000 );
		if( c == 'x' ) {
			printf("\nExit.\n");
			return 0;
		}
		else {
			c=c-'0';
			if( ( c < 0 ) || ( c > numdev ) ){
				c=-1;
			}
		}
	}

	printf("\rOkay, trying to init %s\n", devices[c]->d_name );
	sleep(1);
	getConfig()->rcdev=strdup(devices[c]->d_name);
	fd=initHID();
	if( fd == -1 ) {
		printf("Failed to use %s!\n", devices[c]->d_name );
		return -1;
	}

	printf("\nStarting training. Press the button to use for the printed\n");
	printf("command within three seconds, otherwise the command will be\n");
	printf("unassigned.\n\n");

	do {
		printf("Press any key to start..\n");
		while( getch(1000) == -1 );

		for( i = 0; i<MPRC_NUM; i++ ) {
			printf("Code for %s: ", _mprccmdstrings[i] ); fflush(stdout);
			if( getEventCode( &c, fd, 3000, 0) == -1 ) {
				printf("NONE\n");
			}
			else {
				printf("%i\n", c );
				getConfig()->rccodes[i]=c;
			}
			sleep(1);
		}
		printf("Okay? (y)\n");
		while( ( c=getch(1000) ) == -1 );
	} while ( c != 'y' );

	writeConfig(NULL);
}
