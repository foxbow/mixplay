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

#include "config.h"
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
	char *pass;
	pass=falloc(8,1);
	strcpy(pass,"mixplay");

	addMessage(0, "External quit on signal %i!", signo );
	if( getConfig()->command == mpc_quit ) {
		addMessage( 0, "Forced exit!!" );
		unlink(PIDPATH);
		exit(1);
	}
	/* try nicely first */
	setCommand( mpc_quit, pass );
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

	unlink(PIDPATH);
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

/*
 * keyboard support for -d
 */
static char _lasttitle[MAXPATHLEN+1];
static mpcmd_t _last=mpc_start;

static void debugHidPrintline( const char* text, ... ){
	va_list args;
	printf( "\r" );
	va_start( args, text );
	vprintf( text, args );
	va_end( args );
	printf( "\nMP> " );
	fflush( stdout );
}

/*
 * print title and play changes
 */
static void _debugHidUpdateHook() {
	char *title=NULL;

	if( ( getConfig()->current!=NULL ) &&
	( getConfig()->current->title != NULL ) ) {
		title=getConfig()->current->title->display;
	}

	/* has the title changed? */
	if ( ( title != NULL ) && ( strcmp( title, _lasttitle ) != 0 ) ) {
		strtcpy( _lasttitle, title, MAXPATHLEN );
		debugHidPrintline("Now playing: %s",title);
	}

	/* has the status changed? */
	if( getConfig()->status != _last ) {
		_last=getConfig()->status;
		switch(_last) {
			case mpc_idle:
				debugHidPrintline("[PAUSE]");
				break;
			case mpc_play:
				debugHidPrintline("[PLAY]");
				break;
			default:
				/* ignored */
				break;
		}
	}
}

static int hidCMD( int c ) {
	const char keys[MPRC_NUM]=" pnfd-.,";
	int i;

	if( c == -1 ) {
		return 0;
	}

	for( i=0; i<MPRC_NUM; i++ ) {
		if( c == keys[i] ) {
			setCommand( _mprccmds[i], NULL );
			return 0;
		}
	}

	if ( c == 'Q' ) {
		debugHidPrintline("[QUIT]");
		setCommand( mpc_quit, strdup("mixplay") );
		return 0;
	}

	return -1;
}

/* the most simple HID implementation for -d */
static void debugHID( ) {
	int c;
	mpconfig_t *config=getConfig();

	/* wait for the initialization to be done */
	while( ( config->status != mpc_play ) &&
	       ( config->status != mpc_quit ) ){
		sleep(1);
	}

	while( config->status != mpc_quit ) {
		c=getch( 750 );
		hidCMD(c);
	}
}

int main( int argc, char **argv ) {
	mpconfig_t	*control;
	FILE *pidlog=NULL;
	struct timeval tv;
	int hidfd=-1;

	control=readConfig( );
	if( control == NULL ) {
		printf("Cannot find configuration!\n");
		printf("Run 'mprcinit' first\n");
		return 1;
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

	if( access( PIDPATH, F_OK ) == 0 ) {
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
		pidlog=fopen( PIDPATH, "w");
		if( pidlog == NULL ) {
			addMessage( 0, "Cannot open %s!", PIDPATH );
			addError(errno);
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
	hidfd=initHID();
 	if( hidfd != -1 ) {
		startHID( hidfd );
	}

	if( getDebug() == 1 ) {
		addUpdateHook( &_debugHidUpdateHook );
		debugHID();
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
	unlink(PIDPATH);
	addMessage( 0, "Player terminated" );
	freeConfig( );

	return 0;
}
