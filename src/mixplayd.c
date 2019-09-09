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
#include <sys/time.h>
#include <errno.h>

#include "utils.h"
#include "player.h"
#include "mpinit.h"
#ifdef EPAPER
#include "mpepa.h"
#endif
#include "mphid.h"

static unsigned long _curmsg=0;

/**
 * TODO: create a dedicated signal handler thread.
 **/
static void sigint(int signo){
	addMessage(0, "External quit on signal %i!", signo );
	if( getConfig()->command == mpc_quit ) {
		addMessage( 0, "Forced exit!!" );
		unlink(getConfig()->pidpath);
		exit(1);
	}
	/* brute force to avoid lockups */
	getConfig()->command=mpc_quit;
	getConfig()->status=mpc_quit;
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
	printf("mixplayd: ");
	va_start( args, msg );
	vfprintf( stdout, msg, args );
	if( getConfig()->isDaemon ) {
		vsyslog( LOG_ERR, msg, args );
	}
	va_end( args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
		if( getConfig()->isDaemon ) {
			syslog( LOG_ERR, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
		}
	}

	unlink(getConfig()->pidpath);
#ifdef EPAPER
	epExit();
#endif
	getConfig()->command=mpc_quit;
	getConfig()->status=mpc_quit;

	pthread_join( getConfig()->stid, NULL );
	pthread_join( getConfig()->rtid, NULL );

	exit( error );
}

/**
 * special handling for the server during information updates
 */
static void s_updateHook( ) {
	mpconfig_t *data=getConfig();
	if( _curmsg < data->msg->count ) {
		if( data->isDaemon ) {
			syslog( LOG_NOTICE, "%s", msgBuffPeek( data->msg, _curmsg ) );
		}
		_curmsg++;
	}
}

int main( int argc, char **argv ) {
	mpconfig_t	*control;
	char *path;
	FILE *pidlog=NULL;
	struct timeval tv;

	control=readConfig( );
	if( control == NULL ) {
		printf( "music directory needs to be set.\n" );
		printf( "It will be set up now\n" );
		path=(char *)falloc( MAXPATHLEN+1, 1 );
		while( 1 ) {
			printf( "Default music directory:" );
			fflush( stdout );
			memset( path, 0, MAXPATHLEN );
			if( fgets(path, MAXPATHLEN, stdin ) == NULL ) {
				continue;
			};
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

	/* improve 'randomization' */
	gettimeofday( &tv,NULL );
	srand( (getpid()*tv.tv_usec)%RAND_MAX );

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
				addMessage( -1, "No music found at %s!", control->musicdir );
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
		addMessage( 0, "Unknown argument '%s'!", argv[optind] );
		return -1;
	}

	snprintf( control->pidpath, MAXPATHLEN, "%s/.mixplay/mixplayd.pid", getenv("HOME") );
	if( access( control->pidpath, F_OK ) == 0 ) {
		addMessage( 0, "Mixplayd is already running!" );
		freeConfig();
		return -1;
	}

	/* TODO: this is a bad idea in a multithreaded environment! */
	signal(SIGINT, sigint );
	signal(SIGTERM, sigint );

	/* daemonization must happen before childs are created otherwise the pipes are cut */
	if( getDebug() == 0 ) {
		if( daemon( 1, 1 ) != 0 ) {
			fail( errno, "Could not demonize!" );
		}
		openlog ("mixplayd", LOG_PID, LOG_DAEMON);
		control->isDaemon=1;
		pidlog=fopen( control->pidpath, "w");
		if( pidlog == NULL ) {
			addMessage( 0, "Cannot open %s!", control->pidpath );
			return -1;
		}
		fprintf( pidlog, "%i", getpid() );
		fclose(pidlog);
	}

	addUpdateHook( &s_updateHook );

	control->inUI=1;
	initAll( );
	#ifdef EPAPER
	sleep(1);
	if( control->status != mpc_quit ) {
		epSetup();
		addUpdateHook( &epUpdateHook );
	}
	#endif
	if( getDebug() ) {
		runHID();
	}
	pthread_join( control->stid, NULL );
	pthread_join( control->rtid, NULL );
	control->inUI=0;
#ifdef EPAPER
	epExit();
#endif
	if( control->changed ) {
		writeConfig( NULL );
	}
	unlink(control->pidpath);
	addMessage( 0, "Player terminated" );
	freeConfig( );

	return 0;
}
