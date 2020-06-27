#include <stdio.h>
#include <unistd.h>

#include "musicmgr.h"
#include "utils.h"
#include "player.h"
#include "mpserver.h"

/*
 * print out the default CLI help text
 */
static void printUsage( char *name ) {
	printf( "USAGE: %s [args] [resource]\n", name );
	printf( " -v : increase debug message level to display in app\n" );
	printf( " -V : print curent build ID\n" );
	printf( " -d : increase debug message level to display on console\n" );
	printf( " -f : single channel - disable fading\n" );
	printf( " -F : enable fading\n");
	printf( " -h : print help\n" );
	printf( " -p <port> : set port [2347]\n" );
	printf( " -m : force mix on playlist\n" );
	printf( " -W': write changed config (used with -r,-l,-h,-p)\n" );
	printf( " resource: resource to play\n" );
	printf( "		   URL, directory, mp3 file, playlist\n" );
}

static mpplaylist_t *titleToPlaylist( mptitle_t *title, mpplaylist_t *pl ) {
	mptitle_t *guard=title;

	pl=wipePlaylist(pl);

	do {
		pl=appendToPL( title, pl, -1 );
		title=title->next;
	} while( title != guard );

	while( pl->prev != NULL ) {
		pl=pl->prev;
	}

	return pl;
}

/**
 * parse arguments given to the application
 * also handles playing of a single file, a directory, a playlist or an URL
 * this is also called after initialization, so the PM_SWITCH flag does
 * actually make sense here.
 */
int setArgument( const char *arg ) {
	mptitle_t *title=NULL;
	char line [MAXPATHLEN+1];
	mpconfig_t *control=getConfig();

	control->active=0;
	control->mpmode=PM_NONE;

	if( isURL( arg ) ) {
		addMessage( 1, "URL: %s", arg );
		line[0]=0;

		if( strstr( arg, "https" ) == arg ) {
			addMessage( 0, "No HTTPS support, trying plain HTTP." );
			strtcat( line, "http", MAXPATHLEN );
			strtcat( line, arg+5, MAXPATHLEN );
		}
		else {
			strtcpy( line, arg, MAXPATHLEN );
		}
		control->mpmode=PM_STREAM|PM_SWITCH;
		setStream( line, "<connecting>" );
		return 1;
	}
	else if( endsWith( arg, ".mp3" ) ) {
		addMessage( 1, "Single file: %s", arg );
		/* play single song... */
		control->mpmode=PM_PLAYLIST|PM_SWITCH;
		title=insertTitle( NULL, arg );
		if( title != NULL ) {
			control->root=wipeTitles( control->root );
			control->current=titleToPlaylist( title, control->current );
		}
		return 2;
	}
	else if( isDir( arg ) ) {
		if( arg[0] != '/' ) {
			addMessage( 0, "%s is not an absolute path!", arg );
		}
		else {
			addMessage( 1, "Directory: %s", arg );
		}
		strncpy( line, arg, MAXPATHLEN );
		title=recurse( line, NULL );
		if( title != NULL ) {
			control->mpmode=PM_PLAYLIST|PM_SWITCH;
			control->root=wipeTitles( control->root );
			if( control->mpmix ) {
				control->root=title;
				plCheck(0);
			}
			else {
				control->current=titleToPlaylist( title, control->current );
			}
		}
		return 3;
	}
	else if ( endsWith( arg, ".m3u" ) ||
			  endsWith( arg, ".pls" ) ) {
		addMessage( 1, "Playlist: %s", arg );
		control->mpmode=PM_PLAYLIST|PM_SWITCH;
		title=loadPlaylist( arg );
		if( title != NULL ) {
			if( control->mpmix ) {
				control->current=wipePlaylist( control->current );
				control->root=wipeTitles( control->root );
				control->root=title;
				plCheck(0);
			}
			else {
				control->current=titleToPlaylist( title, control->current );
				control->root=wipeTitles( control->root );
			}
		}
		return 4;
	}

	fail( F_FAIL, "Illegal argument '%s'!", arg );
	return F_FAIL;
}

/*
 * parses the given flags and arguments
 */
int getArgs( int argc, char ** argv ){
	mpconfig_t *config=getConfig();
	int c;

	/* parse command line options */
	/* using unsigned char c to work around getopt quirk on ARM */
	while ( ( c = getopt( argc, argv, "vVfdFh:p:Wm" ) ) != -1 ) {
		switch ( c ) {
		case 'v': /* increase debug message level to display */
			incVerbosity();
			break;

		case 'V':
			printf("%s version %s\n", argv[0], VERSION );
			exit( EXIT_SUCCESS );

		case 'd': /* increase debug message level to display on console */
			incDebug();
			break;

		case 'f': /* single channel - disable fading */
			config->fade=0;
			break;

		case 'F': /* enable fading */
			config->fade=1;
			break;

		case 'h':
			printUsage( argv[0] );
			exit( 0 );
			break;

		case 'p':
			config->port=atoi(optarg);
			break;

		case 'W':
			config->changed=1;
			break;

		case 'm':
			config->mpmix=1;
			break;

		case '?':
			switch( optopt )  {
				case 'h':
				case 'p':
					fprintf (stderr, "Option -%c requires an argument!\n", optopt);
				break;
				default:
					printf( "Unknown option -%c\n", optopt );
				break;
			}
			/* fallthrough */

		default:
			printUsage( argv[0] );
			exit( EXIT_FAILURE );
		}
	}

	if ( optind < argc ) {
		return setArgument( argv[optind] );
	}

	if( config->changed ) {
		writeConfig( NULL );
	}
	return 0;
}

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 *
 * this will also start the communication thread is remote=2
 */
int initAll( ) {
	mpconfig_t *control;
	pthread_t tid;
	struct timespec ts;
	control=getConfig();
	ts.tv_sec=0;
	ts.tv_nsec=250000;

	/* start the comm server */
	if( startServer() != 0 ) {
		return -1;
	}

	/* start the actual player */
	pthread_create( &control->rtid, NULL, reader, NULL );
	/* make sure the mpg123 instances have a chance to start up */
	nanosleep(&ts, NULL);
	if( NULL == control->root ) {
		/* Runs as thread to have updates in the UI */
		pthread_create( &tid, NULL, setProfile, NULL );
		pthread_detach( tid );
	}
	else {
		control->active=0;
		control->dbname[0]=0;
		setCommand( mpc_play, NULL );
	}

	return 0;
}
