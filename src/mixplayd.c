/*
 * mixplayd.c
 *
 * mixplay demon that plays headless and offers a control channel
 * through an IP socket
 *
 *  Created on: 16.11.2017
 *	  Author: bweber
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>

#include "utils.h"
#include "player.h"

static unsigned long _curmsg=0;

static void sigint(int signo){
	addMessage(0, "External quit!", signo);
	setCommand(mpc_quit);
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
	va_start( args, msg );

	if( getConfig()->isDaemon ) {
		vsyslog( LOG_ERR, msg, args );
	}
	fprintf( stdout, "\n" );
	printf("mixplayd: ");
	vfprintf( stdout, msg, args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
		if( getConfig()->isDaemon ) {
			syslog( LOG_ERR, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
		}
	}
	va_end( args );

	unlink(getConfig()->pidpath);
	exit( error );
}

/**
 * special handling for the server during information updates
 */
void s_updateHook( void *ignore ) {
	mpconfig *data=getConfig();
	if( _curmsg < data->msg->count ) {
		if( data->isDaemon ) {
			syslog( LOG_NOTICE, "%s", msgBuffPeek( data->msg, _curmsg ) );
		}
		_curmsg++;
	}
}

int main( int argc, char **argv ) {
	mpconfig	*control;
	char *path;
	FILE *pidlog=NULL;

	control=readConfig( );
	if( control == NULL ) {
		printf( "music directory needs to be set.\n" );
		printf( "It will be set up now\n" );
		path=(char *)falloc( MAXPATHLEN+1, 1 );
		while( 1 ) {
			printf( "Default music directory:" );
			fflush( stdout );
			memset( path, 0, MAXPATHLEN );
			fgets(path, MAXPATHLEN, stdin );
			path[strlen( path )-1]=0; /* cut off CR */
			abspath( path, getenv( "HOME" ), MAXPATHLEN );

			if( isDir( path ) ) {
				break;
			}
			else {
				printf( "%s is not a directory!\n", path );
			}
		}

		writeConfig( path );
		free( path );
		control=readConfig();
		if( control == NULL ) {
			printf( "Could not create config file!\n" );
			return 1;
		}
	}
	muteVerbosity();

	snprintf( control->pidpath, MAXPATHLEN, "%s/.mixplay/mixplayd.pid", getenv("HOME") );
	if( access( control->pidpath, F_OK ) == 0 ) {
		addMessage( 0, "Mixplayd is already running!" );
		freeConfig();
		return -1;
	}

	switch( getArgs( argc, argv ) ) {
	case 0: /* no arguments given */
		break;

	case 1: /* stream - does this even make sense? */
		break;

	case 2: /* single file */
		break;

	case 3: /* directory */
		/* if no default directory is set, use the one given */
		if( control->musicdir == NULL ) {
			incDebug();
			addMessage( 0, "Setting default configuration values and initializing..." );
			setProfile( control );
			if( control->root == NULL ) {
				addMessage( 0, "No music found at %s!", control->musicdir );
				return -1;
			}
			addMessage( 0, "Initialization successful!" );
			writeConfig( argv[optind] );
			freeConfig( );
			return 0;
		}
		break;
	case 4: /* playlist */
		break;
	default:
		addMessage( 0, "Unknown argument!\n", argv[optind] );
		return -1;
	}

	signal(SIGINT, sigint );

	/* daemonization must happen before childs are created otherwise the pipes are cut */
	if( getDebug() == 0 ) {
		daemon( 0, 1 );
		openlog ("mixplayd", LOG_PID, LOG_DAEMON);
		control->isDaemon=1;
		pidlog=fopen( control->pidpath, "w");
		if( pidlog == NULL ) {
			addMessage( 0, "Cannot open %s!", control->pidpath );
			return -1;
		}
		fprintf( pidlog, "%li", pthread_self() );
		fclose(pidlog);
	}

	addUpdateHook( &s_updateHook );
	control->inUI=1;
	initAll( );
	pthread_join( control->stid, NULL );
	pthread_join( control->rtid, NULL );
	control->inUI=0;
	if( control->changed ) {
		writeConfig( NULL );
	}
	unlink(control->pidpath);
	addMessage( 0, "Daemon terminated" );
	freeConfig( );

	return 0;
}
