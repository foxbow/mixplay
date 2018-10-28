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

#include "utils.h"
#include "player.h"

static int _ftrpos=0;
static unsigned long _curmsg=0;
static int _actmsg=0;
static char *_lastact=NULL;

/**
 * show activity roller on console
 * this will only show if debug mode is enabled
 */
void activity( const char *msg, ... ) {
	char roller[5]="|/-\\";
	char text[256]="";
	int pos;
	va_list args;

	va_start( args, msg );
	vsprintf( text, msg, args );
	va_end( args );

	if( ( _lastact == NULL) || ( strcmp( _lastact, msg ) != 0 ) ) {
		_actmsg=0;
	}

	if( getDebug() ) {
		_ftrpos=( _ftrpos+1 )%( 400/getDebug() );
		if ( _ftrpos%( 100/getDebug() ) == 0 ) {
			pos=( _ftrpos/( 100/getDebug() ) )%4;
			printf( "%c %s                                  \r", roller[pos], text );
			fflush( stdout );
		}
	}

	if( ( _actmsg++ )%200 == 0 ) {
		addMessage( 0, text );
	}
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

	/* this will only run as server */
	control->remote=2;

	/* daemonization must happen before childs are created otherwise the pipes are cut */
	if( control->isDaemon || getDebug() == 0 ) {
		daemon( 0, 1 );
		openlog ("mixplayd", LOG_PID, LOG_DAEMON);
		control->isDaemon=1;
	}

	addUpdateHook( &s_updateHook );
	control->inUI=1;
	initAll( );
	pthread_join( control->stid, NULL );
	pthread_join( control->rtid, NULL );
	control->inUI=0;
	addMessage( 0, "Daemon terminated" );
	freeConfig( );

	return 0;
}
