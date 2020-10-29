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
#include "mphid.h"
#include "mpflirc.h"
#include "mpserver.h"

static unsigned long _curmsg=0;

/**
 * TODO: create a dedicated signal handler thread.
 **/
static void sigint(__attribute__((unused)) int signo){
	unlink(PIDPATH);
	abort();
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
	getConfig()->command=mpc_quit;
	getConfig()->status=mpc_quit;

	pthread_join( getConfig()->stid, NULL );
	pthread_join( getConfig()->rtid, NULL );

	abort();
}

/**
 * special handling for the server during information updates
 */
static void s_updateHook( ) {
	mpconfig_t *data=getConfig();
	if( _curmsg < data->msg->count ) {
		if( data->isDaemon ) {
			syslog( LOG_NOTICE, "%s", msgBuffPeek( data->msg, _curmsg )->msg );
		}
		_curmsg++;
	}
}

/*
 * keyboard support for -d
 */
static char _lasttitle[MAXPATHLEN+1];
static mpcmd_t _last=mpc_start;

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
		hidPrintline( "%s", title );
	}

	/* has the status changed? */
	if( getConfig()->status != _last ) {
		_last=getConfig()->status;
		hidPrintline("[%s]", mpcString(_last));
	}
}

/* the most simple HID implementation for -d */
static void *debugHID( ) {
	int c;
	mpconfig_t *config=getConfig();
	mpcmd_t cmd;

	/* wait for the initialization to be done */
	while( ( config->status != mpc_play ) &&
	       ( config->status != mpc_quit ) ){
		sleep(1);
	}

	while( config->status != mpc_quit ) {
		c=getch( 750 );
		cmd=hidCMD(c);
		if( cmd == mpc_quit ) {
			hidPrintline("[QUIT]");
			setCommand( mpc_quit, strdup(getConfig()->password) );
		}
		else if( cmd != mpc_idle ) {
			setCommand(cmd, NULL);
		}
	}
	return NULL;
}

int main( int argc, char **argv ) {
	mpconfig_t	*control;
	FILE *pidlog=NULL;
	struct timeval tv;
	int hidfd=-1;
	int rv=0;
	int res=0;
	pthread_t hidtid=0;

	/* first of all check if there isn't already another instance running */
	if( access( PIDPATH, F_OK ) == 0 ) {
		pidlog=fopen(PIDPATH, "r");
		if( pidlog == NULL ) {
			addMessage(0, "Cannot open %s!", PIDPATH);
			addError(errno);
			return -1;
		}
		res = fscanf( pidlog, "%i", &rv );
		fclose(pidlog);
		if( res != 1 ) {
			addMessage(0, "Could not read PID from %s!", PIDPATH);
			return -1;
		}
		/* does the pid exist? */
		if(!kill(rv,0) && !errno) {
			/* a process is using the PID. It's highly likely that this is in fact
			 * the mixplayd that created the pidfile but there is a chance that
			 * some other process recycled the PID after a reboot or something */
			addMessage( 0, "Mixplayd (PID: %i) is already running!", rv );
			return -1;
		}
		addMessage( 0, "Found stale pidfile, you may want to check for a core!" );
		unlink(PIDPATH);
	}

	control=readConfig( );
	if( control == NULL ) {
		printf("Cannot find configuration!\n");
		printf("Run 'mprcinit' first\n");
		return 1;
	}

	/* improve 'randomization' */
	gettimeofday( &tv,NULL );
	srandom( (getpid()*tv.tv_usec)%RAND_MAX );

	rv=getArgs( argc, argv );
	if( rv < 0 ) {
		addMessage( 0, "Unknown argument '%s'!", argv[optind] );
		return -1;
	}

	/* if no default directory is set, use the one given */
	if ( ( rv == 3 ) && ( control->musicdir == NULL ) ) {
		incDebug();
		addMessage( 0, "Setting default configuration values and initializing..." );
		setProfile(NULL);
		if( control->root == NULL ) {
			addMessage( -1, "No music found at %s!", control->musicdir );
			return -1;
		}
		addMessage( 0, "Initialization successful!" );
		writeConfig( argv[optind] );
		freeConfig( );
		return 0;
	}

	/* plays with parameter should not detach */
	if( (rv > 0) && !getDebug() ) {
		incDebug();
	}

	/* TODO: this is a bad idea in a multithreaded environment! */
	signal(SIGINT, sigint );
	signal(SIGTERM, sigint );

	/* daemonization must happen before childs are created otherwise the pipes
	   are cut TODO: what about daemon(1,0)? */
	if( getDebug() == 0 ) {
		if( daemon( 1, 1 ) != 0 ) {
			/* one of the few really fatal cases! */
			addMessage( 0, "Could not demonize!" );
			addError(errno);
			return -1;
		}
		openlog ("mixplayd", LOG_PID, LOG_DAEMON);
		control->isDaemon=1;
	}

	if( access( PIDPATH, F_OK ) != 0 ) {
		pidlog=fopen( PIDPATH, "w");
		if( pidlog == NULL ) {
			addMessage( 0, "Cannot open %s!", PIDPATH );
			addError(errno);
			return -1;
		}
		fprintf( pidlog, "%i", getpid() );
		fclose(pidlog);
	}
	else {
		addMessage(0, "Another mixplayd was quicker!");
		return -1;
	}

	addUpdateHook( &s_updateHook );

	if( !startServer() && !initAll() ) {
		/* flirc handler */
		hidfd=initFLIRC();
		if( hidfd != -1 ) {
			startFLIRC( hidfd );
		}

		if( getDebug() ) {
			addUpdateHook( &_debugHidUpdateHook );
			pthread_create( &hidtid, NULL, debugHID, NULL );
		}
		/**
		 * wait for the reader thread to terminate. If it terminated due to the
		 * watchdog, restart it and wait again, otherwise commence shutdown.
		 */
		do {
			addMessage(1, "Waiting for reader to stop");
			pthread_join( control->rtid, NULL );
			addMessage(1, "Reader stopped");
			if( control->watchdog > STREAM_TIMEOUT ) {
				addMessage(-1, "Restarting reader");
				initAll();
			}
		} while (control->watchdog > STREAM_TIMEOUT );
		addMessage(1, "Waiting for server to stop");
		pthread_join( control->stid, NULL );
		if( hidtid > 0) {
			addMessage(1, "Waiting for HID to stop");
			pthread_join( hidtid, NULL );
		}
		addMessage(1, "Server stopped");
		control->inUI=0;
		addMessage( 0, "Player terminated gracefully" );
	}

	if( access( PIDPATH, F_OK ) == 0 ) {
		unlink(PIDPATH);
	}

	freeConfig( );

	return 0;
}
