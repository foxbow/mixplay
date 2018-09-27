/*
 * config.c
 *
 * handles reading and writing the configuration
 *
 * the configuration file looks like a standard GTK configuration for historical reasons.
 * It should also parse with the gtk_* functions but those are not always available in headless
 * environments.
 *
 *  Created on: 16.11.2017
 *	  Author: bweber
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <getopt.h>
#include <unistd.h>

#include "utils.h"
#include "musicmgr.h"
#include "config.h"
#include "mpcomm.h"
#include "player.h"
#include "mpserver.h"

static pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;
static mpconfig *c_config=NULL;

/**
 * the progress function list
 */
typedef struct _mpFunc_t _mpFunc;
struct _mpFunc_t {
	void (*func)( void * );
	_mpFunc *next;
};

static _mpFunc *_pfunc=NULL;
static _mpFunc *_ufunc=NULL;

static const char *mpc_command[] = {
		"mpc_play",
		"mpc_stop",
		"mpc_prev",
		"mpc_next",
		"mpc_start",
		"mpc_repl",
		"mpc_profile",
		"mpc_quit",
		"mpc_dbclean",
		"mpc_fav",
		"mpc_dnp",
		"mpc_doublets",
		"mpc_NOP",
		"mpc_ivol",
		"mpc_dvol",
		"mpc_bskip",
		"mpc_fskip",
		"mpc_QUIT",
		"mpc_dbinfo",
		"mpc_search",
		"mpc_longsearch",
		"mpc_setvol",
		"mpc_newprof",
		"mpc_path",
		"mpc_remprof",
		"mpc_idle"
};

/*
 * transform an mpcmd value into a string literal
 */
const char *mpcString( mpcmd rawcmd ) {
	mpcmd cmd=MPC_CMD(rawcmd);
	if( cmd <= mpc_idle ) {
		return mpc_command[cmd];
	}
	else {
		addMessage( 1, "Unknown command code %i", cmd );
		return "mpc_idle";
	}
}

/*
 * transform a string literal into an mpcmd value
 */
const mpcmd mpcCommand( const char *name ) {
	int i;
	for( i=0; i<= mpc_idle; i++ ) {
		if( strstr( name, mpc_command[i] ) ) break;
	}
	if( i>mpc_idle ) {
		addMessage( 1, "Unknown command %s!", name );
		return mpc_idle;
	}
	return i;
}

/*
 * sets the current command and decides if it needs to be sent to
 * a remote player or just be handled locally
 */
void setCommand( mpcmd cmd ) {
	mpconfig *config;
	config=getConfig();

	if( cmd == mpc_idle ) {
		return;
	}

	if( config->remote==1 ) {
		if( cmd == mpc_quit ) {
			config->status = mpc_quit;
		}
		else {
			setSCommand( cmd );
		}
	}
	else {
		setPCommand( cmd );
	}
}

/*
 * print out the default CLI help text
 */
static void printUsage( char *name ) {
	addMessage( 0, "USAGE: %s [args] [resource]", name );
	addMessage( 0, " -v : increase debug message level to display in app" );
/*	addMessage( 0, " -S : run in fullscreen mode (gmixplay)" );*/
	addMessage( 0, " -d : increase debug message level to display on console" );
	addMessage( 0, " -f : single channel - disable fading" );
	addMessage( 0, " -F : enable fading");
	addMessage( 0, " -r : control remote mixplayd (see -p and -h)" );
	addMessage( 0, " -l : play local music" );
	addMessage( 0, " -h <addr> : set remote host" );
	addMessage( 0, " -p <port> : set communication port [2347]" );
	addMessage( 0, " -s : start HTTP server" );
	addMessage( 0, " -W': write changed config (used with -r,-l,-h,-p)" );
	addMessage( 0, " resource: resource to play" );
	addMessage( 0, "		   URL, path, mp3 file, playlist\n" );
}

static mpplaylist *titleToPlaylist( mptitle *title, mpplaylist *pl ) {
	mptitle *guard=title;

	pl=cleanPlaylist(pl);

	do {
		pl=appendToPL( title, pl );
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
 */
int setArgument( const char *arg ) {
	mptitle *title=NULL;
	char line [MAXPATHLEN];
	int  i;
	mpconfig *control=getConfig();

	control->active=0;
	if( isURL( arg ) ) {
		addMessage( 1, "URL: %s", arg );
		control->playstream=1;
		line[0]=0;

		if( endsWith( arg, ".m3u" ) ||
				endsWith( arg, ".pls" ) ) {
			fail( F_FAIL, "Only direct stream support" );
			strcpy( line, "@" );
		}

		strncat( line, arg, MAXPATHLEN );
		setStream( line, "Waiting for stream info..." );
		return 1;
	}
	else if( endsWith( arg, ".mp3" ) ) {
		addMessage( 1, "Single file: %s", arg );
		/* play single song... */
		title=insertTitle( NULL, arg );
		if( title != NULL ) {
			cleanTitles( control->root );
			control->current=titleToPlaylist( title, control->current );
		}
		return 2;
	}
	else if( isDir( arg ) ) {
		addMessage( 1, "Directory: %s", arg );
		strncpy( line, arg, MAXPATHLEN );
		title=recurse( line, NULL );
		if( title != NULL ) {
			cleanTitles( control->root );
			control->current=titleToPlaylist( title, control->current );
		}
		return 3;
	}
	else if ( endsWith( arg, ".m3u" ) ||
			  endsWith( arg, ".pls" ) ) {
		if( NULL != strrchr( arg, '/' ) ) {
			strcpy( line, arg );
			i=strlen( line );

			while( line[i] != '/' ) {
				i--;
			}

			line[i]=0;
			chdir( line );
		}

		addMessage( 1, "Playlist: %s", arg );
		title=loadPlaylist( arg );
		if( title != NULL ) {
			cleanTitles( control->root );
			control->current=titleToPlaylist( title, control->current );
		}
		return 4;
	}

	fail( F_FAIL, "Illegal argument '%s'!", arg );
	return -1;
}

/*
 * parses the given flags and arguments
 */
int getArgs( int argc, char ** argv ){
	mpconfig *config=getConfig();
	unsigned char c;

	/* parse command line options */
	/* using unsigned char c to work around getopt quirk on ARM */
	while ( ( c = getopt( argc, argv, "vfldFrh:p:sW" ) ) != 255 ) {
		switch ( c ) {
		case 'v': /* increase debug message level to display */
			incVerbosity();
			break;

/*		case 'S':
			if( strcmp( argv[0], "gmixplay" ) == 0 ) {
				glcontrol.fullscreen=1;
			}
			else {
				addMessage( 0, "-S not supported for %s!", argv[0] );
			}
			break;
*/
		case 'd': /* increase debug message level to display on console */
			incDebug();
			break;

		case 'f': /* single channel - disable fading */
			config->fade=0;
			break;

		case 'F': /* enable fading */
			config->fade=1;
			break;

		case 'r':
			config->remote=1;
			break;

		case 'l':
			config->remote=0;
			break;

		case 'h':
			sfree( &(config->host) );
			config->host=falloc( strlen(optarg)+1, sizeof(char) );
			strcpy( config->host, optarg );
			config->remote=1;
			break;

		case 'p':
			config->port=atoi(optarg);
			break;

		case 's':
			config->remote=2;
			break;

		case 'W':
			config->changed=-1;
			break;

		case '?':
			switch( optopt )  {
			case 'h':
			case 'p':
				fprintf (stderr, "Option -%c requires an argument!\n", optopt);
				break;
			default:
				addMessage( 0, "Unknown option -%c\n", optopt );
			}
			/* no break */

		default:
			printUsage( argv[0] );
			exit( EXIT_FAILURE );
		}
	}

	if ( optind < argc ) {
		return setArgument( argv[optind] );
	}

	if( config->changed == -1 ) {
		writeConfig( NULL );
	}
	return 0;
}

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 *
 * if dispatch is != 0, the profile will be set in a separate thread.
 * this is handy if the main thread is used to display messages e.g.:
 * gmixplay
 *
 * this will also start the communication thread is remote=2
 */
int initAll( ) {
	mpconfig *control;
	pthread_t tid;
	struct timespec ts;
	control=getConfig();
	ts.tv_sec=0;
	ts.tv_nsec=250000;

	if ( control->remote == 1 ) {
		pthread_create( &control->rtid, NULL, netreader, (void *)control );
		control->playstream=0;
	}
	else {
		/* start the actual player */
		pthread_create( &control->rtid, NULL, reader, (void *)control );
		/* make sure the mpg123 instances have a chance to start up */
		nanosleep(&ts, NULL);
		if( NULL == control->root ) {
			/* Runs as thread to have updates in the UI */
			pthread_create( &tid, NULL, setProfile, ( void * )control );
			pthread_detach( tid );
		}
		else {
			control->active=0;
			control->dbname[0]=0;
			setCommand( mpc_play );
		}
	}

	/*
	 * start the comm server if requested
	 */
	if ( control->remote == 2 ) {
		pthread_create( &control->stid, NULL, mpserver, NULL );
	}

	return 0;
}

/**
 * returns the current configuration
 */
mpconfig *getConfig() {
	assert( c_config != NULL );
	return c_config;
}

/**
 * parses a multi-string config value in the form of:
 * val1;val2;val3;
 *
 * returns the number of found values
 */
static int scanparts( char *line, char ***target ) {
	int i;
	char *pos;
	int num=0;

	/* count number of entries */
	for ( i=0; i<strlen(line); i++ ) {
		if( line[i]==';' ) {
			num++;
		}
	}

	/* walk through the entries */
	if( num > 0 ) {
		*target=falloc( num, sizeof( char * ) );
		for( i=0; i<num; i++ ) {
			pos=strchr( line, ';' );
			*pos=0;
			(*target)[i]=falloc( strlen(line)+1, sizeof( char ) );
			strip( (*target)[i], line, strlen(line)+1 );
			line=pos+1;
		}
	}

	return num;
}

/**
 * reads the configuration file at $HOME/.mixplay/mixplay.conf and stores the settings
 * in the given control structure.
 * returns NULL is no configuration file exists
 *
 * This function should be called more or less first thing in the application
 */
mpconfig *readConfig( ) {
	char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
	char	line[MAXPATHLEN];
	char	*pos;
	FILE	*fp;

	if( c_config == NULL ) {
		c_config=falloc( 1, sizeof( mpconfig ) );
		c_config->msg=msgBuffInit();
	}

	/* Set some default values */
	c_config->root=NULL;
	c_config->current=NULL;
	c_config->playstream=0;
	c_config->volume=80;
	strcpy( c_config->playtime, "00:00" );
	strcpy( c_config->remtime, "00:00" );
	c_config->percent=0;
	c_config->status=mpc_idle;
	c_config->command=mpc_idle;
	c_config->dbname=falloc( MAXPATHLEN, sizeof( char ) );
	c_config->verbosity=0;
	c_config->debug=0;
	c_config->fade=1;
	c_config->inUI=0;
	c_config->msg->lines=0;
	c_config->msg->current=0;
	c_config->host="127.0.0.1";
	c_config->port=MP_PORT;
	c_config->changed=0;
	c_config->isDaemon=0;

	snprintf( c_config->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", getenv("HOME") );

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );
	fp=fopen( conffile, "r" );

	if( NULL != fp ) {
		do {
			fgets( line, MAXPATHLEN, fp );
			if( line[0]=='#' ) continue;
			pos=strchr( line, '=' );
			if( ( NULL == pos ) || ( strlen( ++pos ) == 0 ) ) continue;
			if( strstr( line, "musicdir=" ) == line ) {
				c_config->musicdir=falloc( strlen(pos)+1, sizeof( char ) );
				strip( c_config->musicdir, pos, strlen(pos)+1 );
			}
			if( strstr( line, "channel=" ) == line ) {
				c_config->channel=falloc( strlen(pos)+1, sizeof( char ) );
				strip( c_config->channel, pos, strlen(pos)+1 );
			}
			if( strstr( line, "profiles=" ) == line ) {
				c_config->profiles=scanparts( pos, &c_config->profile );
			}
			if( strstr( line, "streams=" ) == line ) {
				c_config->streams=scanparts( pos, &c_config->stream );
			}
			if( strstr( line, "snames=" ) == line ) {
				if( scanparts( pos, &c_config->sname ) != c_config->streams ) {
					fail( F_FAIL, "Number of streams and stream names does not match!");
				}
			}
			if( strstr( line, "active=" ) == line ) {
				c_config->active=atoi(pos);
			}
			if( strstr( line, "skipdnp=" ) == line ) {
				c_config->skipdnp=atoi(pos);
			}
			if( strstr( line, "fade=" ) == line ) {
				c_config->fade=atoi(pos);
			}
			if( strstr( line, "host=" ) == line ) {
				c_config->host=falloc( strlen(pos)+1, sizeof( char ) );
				strip( c_config->host, pos, strlen(pos)+1 );
			}
			if( strstr( line, "port=" ) == line ) {
				c_config->port=atoi(pos);
			}
			if( strstr( line, "remote=" ) == line ) {
				c_config->remote=atoi(pos);
			}
		}
		while( !feof( fp ) );

		fclose(fp);
		return c_config;
	}

	return NULL;
}

/**
 * writes the configuration from the given control structure into the file at
 * $HOME/.mixplay/mixplay.conf
 */
void writeConfig( const char *musicpath ) {
	char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
	int		i;
	FILE	*fp;

	addMessage( 0, "Saving config" );
	assert( c_config != NULL );

	if( musicpath != NULL ) {
		c_config->musicdir=falloc( strlen(musicpath)+1, sizeof( char ) );
		strip( c_config->musicdir, musicpath, strlen(musicpath)+1 );
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay", getenv("HOME") );
	if( mkdir( conffile, 0700 ) == -1 ) {
		if( errno != EEXIST ) {
			fail( errno, "Could not create config dir %s", conffile );
		}
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );

	fp=fopen( conffile, "w" );

	if( NULL != fp ) {
		fprintf( fp, "[mixplay]" );
		fprintf( fp, "\nmusicdir=%s", c_config->musicdir );
		fprintf( fp, "\nprofiles=" );
		for( i=0; i< c_config->profiles; i++ ) {
			fprintf( fp, "%s;", c_config->profile[i] );
		}
		fprintf( fp, "\nstreams=" );
		for( i=0; i< c_config->streams; i++ ) {
			fprintf( fp, "%s;", c_config->stream[i] );
		}
		fprintf( fp, "\nsnames=" );
		for( i=0; i< c_config->streams; i++ ) {
			fprintf( fp, "%s;", c_config->sname[i] );
		}
		fprintf( fp, "\nactive=%i", c_config->active );
		fprintf( fp, "\nskipdnp=%i", c_config->skipdnp );
		fprintf( fp, "\nfade=%i", c_config->fade );
		if( c_config->channel != NULL ) {
			fprintf( fp, "\nchannel=%s", c_config->channel );
		}
		else {
			fprintf( fp, "channel=Master  for standard installations");
			fprintf( fp, "\n# channel=Digital for HifiBerry");
			fprintf( fp, "\n# channel=Main");
			fprintf( fp, "\n# channel=DAC");
		}
		fprintf( fp, "\nhost=%s", c_config->host );
		if( c_config->port != MP_PORT ) {
			fprintf( fp, "\nport=%i", c_config->port );
		}
		fprintf( fp, "\nremote=%i", c_config->remote );
		fprintf( fp, "\n" );
		fclose( fp );
		c_config->changed=0;
	}
	else {
		fail( errno, "Could not open %s", conffile );
	}
}

/**
 * frees the static parts of the config
 */
void freeConfigContents() {
	int i;
	assert( c_config != NULL );

	sfree( &(c_config->dbname) );
	sfree( &(c_config->dnpname) );
	sfree( &(c_config->favname) );
	sfree( &(c_config->musicdir) );
	for( i=0; i<c_config->profiles; i++ ) {
		sfree( &(c_config->profile[i]) );
	}
	c_config->profiles=0;
	sfree( (char **)&(c_config->profile) );

	for( i=0; i<c_config->streams; i++ ) {
		sfree( &(c_config->stream[i]) );
		sfree( &(c_config->sname[i]) );
	}
	c_config->streams=0;
	sfree( (char **)&(c_config->channel) );

	sfree( (char **)&(c_config->stream) );
	sfree( (char **)&(c_config->sname) );

	sfree( (char **)&(c_config->host) );

	msgBuffDiscard( c_config->msg );
}

/**
 * recursive free() to clean up all of the configuration
 */
void freeConfig( ) {
	assert( c_config != NULL );
	freeConfigContents( );
	c_config->root=cleanTitles( c_config->root );
	free( c_config );
	c_config=NULL;
}

/**
 * adds a message to the message buffer if verbosity is >= v
 *
 * If the application is not in UI mode, the message will just be printed to make sure messages
 * are displayed on the correct media.
 *
 * If debug > v the message is printed on the console (to avoid verbosity 0 messages to
 * always appear in the debug stream.
 */
void addMessage( int v, char *msg, ... ) {
	va_list args;
	char *line;

	pthread_mutex_lock( &msglock );
	line = falloc( MP_MSGLEN, sizeof(char) );
	va_start( args, msg );
	vsnprintf( line, MP_MSGLEN, msg, args );
	va_end( args );

	if( c_config == NULL ) {
		fprintf( stderr, "* %s\n", line );
	}
	else {
		if( v <= getVerbosity() ) {
			if( c_config->inUI ) {
				msgBuffAdd( c_config->msg, line );
				if( v < getDebug() ) {
					fprintf( stderr, "d%i %s\n", v, line );
				}
			}
			else {
				printf( "V %s\n", line );
			}
		}
		else if( v < getDebug() ) {
			fprintf( stderr, "D%i %s\n", v, line );
		}
	}

	free(line);
	pthread_mutex_unlock( &msglock );
}

/**
 * gets the current message removes it from the ring
 */
char *getMessage() {
	return msgBuffGet( c_config->msg );
}

void incDebug( void ) {
	assert( c_config != NULL );
	c_config->debug++;
}

int getDebug( void ) {
	assert( c_config != NULL );
	return c_config->debug;
}

int setVerbosity( int v ) {
	assert( c_config != NULL );
	c_config->verbosity=v;
	return c_config->verbosity;
}

int getVerbosity( void ) {
	assert( c_config != NULL );
	return c_config->verbosity;
}

int incVerbosity() {
	assert( c_config != NULL );
	c_config->verbosity++;
	return c_config->verbosity;
}

void muteVerbosity() {
	setVerbosity(0);
}

static void addHook( void (*func)( void* ), _mpFunc **list ) {
	_mpFunc *pos=*list;
	if( pos == NULL ) {
		*list=falloc(1,  sizeof(_mpFunc) );
		pos=*list;
	}
	else {
		while( pos->next != NULL ) {
			pos=pos->next;
		}
		pos->next=falloc(1,  sizeof(_mpFunc) );
		pos=pos->next;
	}
	pos->func=func;
	pos->next=NULL;
}


void addProgressHook( void (*func)( void * ) ){
	addHook( func, &_pfunc );
}

/*
 * stub implementations
 */
void progressStart( char* msg, ... ) {
	va_list args;
	char *line;
	_mpFunc *pos=_pfunc;

	line=falloc( 512, sizeof( char ) );

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end( args );

	while( pos != NULL ) {
		pos->func( msg );
		pos=pos->next;
	}
	addMessage( 0, line );

	free( line );
}

/**
 * end a progress display
 */
void progressEnd( void ) {
	_mpFunc *pos=_pfunc;

	addMessage( 0, "Done." );
	while( pos != NULL ) {
		pos->func( NULL );
		pos=pos->next;
	}
}

void progressMsg( char *msg ) {
	progressStart( "%s", msg );
	progressEnd();
}

/**
 * register an update function
 */
void addUpdateHook( void (*func)( void * ) ){
	addHook( func, &_ufunc );
}

/**
 * run all registered update functions
 */
void updateUI() {
	_mpFunc *pos=_ufunc;
	while( pos != NULL ) {
		pos->func(NULL);
		pos=pos->next;
	}
}
