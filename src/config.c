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
#include <syslog.h>

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
		"mpc_insert",
		"mpc_ivol",
		"mpc_dvol",
		"mpc_bskip",
		"mpc_fskip",
		"mpc_QUIT",
		"mpc_dbinfo",
		"mpc_search",
		"mpc_append",
		"mpc_setvol",
		"mpc_newprof",
		"mpc_path",
		"mpc_remprof",
		"mpc_edit",
		"mpc_wipe",
		"mpc_save",
		"mpc_remove",
		"mpc_mute",
		"mpc_idle"
};

/*
 * transform an mpcmd value into a string literal
 */
const char *mpcString( mpcmd rawcmd ) {
	int cmd=(int)MPC_CMD(rawcmd);
	if( cmd <= (int)mpc_idle ) {
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
	return (mpcmd)i;
}

/*
 * print out the default CLI help text
 */
static void printUsage( char *name ) {
	printf( "USAGE: %s [args] [resource]", name );
	printf( " -v : increase debug message level to display in app" );
	printf( " -d : increase debug message level to display on console" );
	printf( " -f : single channel - disable fading" );
	printf( " -F : enable fading");
	printf( " -r : control remote mixplayd (see -p and -h)" );
	printf( " -l : play local music" );
	printf( " -h <addr> : set remote host" );
	printf( " -p <port> : set communication port [2347]" );
	printf( " -m : force mix on playlist" );
	printf( " -W': write changed config (used with -r,-l,-h,-p)" );
	printf( " resource: resource to play" );
	printf( "		   URL, path, mp3 file, playlist\n" );
}

static mpplaylist *titleToPlaylist( mptitle *title, mpplaylist *pl ) {
	mptitle *guard=title;

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
 */
int setArgument( const char *arg ) {
	mptitle *title=NULL;
	char line [MAXPATHLEN+1];
	int  i;
	mpconfig *control=getConfig();

	control->active=0;
	control->plplay=0;
	if( isURL( arg ) ) {
		addMessage( 1, "URL: %s", arg );
		control->playstream=1;
		line[0]=0;

		if( endsWith( arg, ".m3u" ) ||
				endsWith( arg, ".pls" ) ) {
			addMessage( 0, "Remote playlist will probably not work.." );
			strcpy( line, "@" );
		}

		if( strstr( arg, "https" ) == arg ) {
			addMessage( 0, "No HTTPS support, trying plain HTTP." );
			strtcat( line, "http", MAXPATHLEN );
			strtcat( line, arg+5, MAXPATHLEN );
		}
		else {
			strtcpy( line, arg, MAXPATHLEN );
		}
		setStream( line, "Waiting for stream info..." );
		return 1;
	}
	else if( endsWith( arg, ".mp3" ) ) {
		addMessage( 1, "Single file: %s", arg );
		/* play single song... */
		title=insertTitle( NULL, arg );
		if( title != NULL ) {
			wipeTitles( control->root );
			control->current=titleToPlaylist( title, control->current );
		}
		return 2;
	}
	else if( isDir( arg ) ) {
		addMessage( 1, "Directory: %s", arg );
		strncpy( line, arg, MAXPATHLEN );
		title=recurse( line, NULL );
		if( title != NULL ) {
			wipeTitles( control->root );
			if( control->plmix==1 ) {
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
		control->plplay=1;
		if( title != NULL ) {
			wipeTitles( control->root );
			if( control->plmix==1 ) {
				control->root=title;
				plCheck(0);
			}
			else {
				control->current=titleToPlaylist( title, control->current );
			}
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
	int c;

	/* parse command line options */
	/* using unsigned char c to work around getopt quirk on ARM */
	while ( ( c = getopt( argc, argv, "vfdFh:p:Wm" ) ) != -1 ) {
		switch ( c ) {
		case 'v': /* increase debug message level to display */
			incVerbosity();
			break;

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
			sfree( &(config->host) );
			config->host=(char*)falloc( strlen(optarg)+1, 1 );
			strcpy( config->host, optarg );
			break;

		case 'p':
			config->port=atoi(optarg);
			break;

		case 'W':
			config->changed=1;
			break;

		case 'm':
			config->plmix=1;
			break;

		case '?':
			switch( optopt )  {
			case 'h':
			case 'p':
				fprintf (stderr, "Option -%c requires an argument!\n", optopt);
				break;
			default:
				printf( "Unknown option -%c\n", optopt );
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
	mpconfig *control;
	pthread_t tid;
	struct timespec ts;
	control=getConfig();
	ts.tv_sec=0;
	ts.tv_nsec=250000;

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

	/*
	 * start the comm server
	 */
	pthread_create( &control->stid, NULL, mpserver, NULL );

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
	unsigned int i;
	char *pos;
	unsigned int num=0;

	/* count number of entries */
	for ( i=0; i<strlen(line); i++ ) {
		if( line[i]==';' ) {
			num++;
		}
	}

	/* walk through the entries */
	if( num > 0 ) {
		*target=(char**)falloc( num, sizeof(char*) );
		for( i=0; i<num; i++ ) {
			pos=strchr( line, ';' );
			if( pos != NULL ) {
				*pos=0;
				(*target)[i]=(char*)falloc( strlen(line)+1, 1 );
				strip( (*target)[i], line, strlen(line) );
				line=pos+1;
			}
			else {
				fail( -1, "Config file Format error in %s!", line );
			}
		}
	}

	return num;
}

/**
 * checks the .mixplay/playlists directory and adds the found playlists
 * to the configuration
 */
void updatePlaylists( void ) {
	char path[MAXPATHLEN+1];
	char *home=getenv("HOME");
	struct dirent **pls=NULL;
	int i;

	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	/* free old entries, yes it would be better to do a 'real' update */
	if( c_config->playlists > 0 ) {
		for( i=0; i<c_config->playlists; i++ ) {
			free(c_config->playlist[i]);
		}
	}

	snprintf( path, MAXPATHLEN, "%s/.mixplay/playlists", home );
	c_config->playlists=getPlaylists( path, &pls );
	if( c_config->playlists < 1 ) {
		c_config->playlists = 0;
	}
	else {
		c_config->playlist=(char**)frealloc( c_config->playlist, c_config->playlists*sizeof(char**) );
		for( i=0; i<c_config->playlists; i++ ) {
			if( strlen( pls[i]->d_name ) > 4 ) {
				c_config->playlist[i]=(char*)falloc( strlen(pls[i]->d_name)-3, 1 );
				strtcpy( c_config->playlist[i], pls[i]->d_name, strlen(pls[i]->d_name)-4 );
				free( pls[i] );
			}
			else {
				addMessage( 0, "Illegal playlist %s", pls[i]->d_name );
				c_config->playlists--;
			}
		}
		free( pls );
	}
}

/**
 * reads the configuration file at $HOME/.mixplay/mixplay.conf and stores the settings
 * in the given control structure.
 * returns NULL is no configuration file exists
 *
 * This function should be called more or less first thing in the application
 */
mpconfig *readConfig( void ) {
	char	conffile[MAXPATHLEN+1]; /*  = "mixplay.conf"; */
	char	line[MAXPATHLEN+1];
	char	*pos;
	char	*home=NULL;
	FILE	*fp;

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	if( c_config == NULL ) {
		c_config=(mpconfig*)falloc( 1, sizeof( mpconfig ) );
		c_config->msg=msgBuffInit();
		c_config->found=(searchresults*)falloc(1,sizeof(searchresults));
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
	c_config->dbname=(char*)falloc( MAXPATHLEN+1, 1 );
	c_config->verbosity=0;
	c_config->debug=0;
	c_config->fade=1;
	c_config->inUI=0;
	c_config->msg->lines=0;
	c_config->msg->current=0;
	c_config->host=(char*)falloc(16,1);
	strcpy( c_config->host, "127.0.0.1" );
	c_config->port=MP_PORT;
	c_config->changed=0;
	c_config->isDaemon=0;
	c_config->streamURL=NULL;

	snprintf( c_config->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", home );

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home );
	fp=fopen( conffile, "r" );

	if( NULL != fp ) {
		do {
			fgets( line, MAXPATHLEN, fp );
			if( line[0]=='#' ) continue;
			pos=strchr( line, '=' );
			if( ( NULL == pos ) || ( strlen( ++pos ) == 0 ) ) continue;
			if( strstr( line, "musicdir=" ) == line ) {
				c_config->musicdir=(char*)falloc( strlen(pos)+1, 1 );
				strip( c_config->musicdir, pos, strlen(pos) );
			}
			if( strstr( line, "channel=" ) == line ) {
				c_config->channel=(char*)falloc( strlen(pos)+1, 1 );
				strip( c_config->channel, pos, strlen(pos) );
			}
			if( strstr( line, "profiles=" ) == line ) {
				c_config->profiles=scanparts( pos, &c_config->profile );
			}
			if( strstr( line, "streams=" ) == line ) {
				c_config->streams=scanparts( pos, &c_config->stream );
			}
			if( strstr( line, "snames=" ) == line ) {
				if( scanparts( pos, &c_config->sname ) != c_config->streams ) {
					fclose(fp);
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
				c_config->host=(char*)frealloc( c_config->host, strlen(pos)+1 );
				strip( c_config->host, pos, strlen(pos) );
			}
			if( strstr( line, "port=" ) == line ) {
				c_config->port=atoi(pos);
			}
		}
		while( !feof( fp ) );

		fclose(fp);

		updatePlaylists();

		return c_config;
	}

	return NULL;
}

/**
 * writes the configuration from the given control structure into the file at
 * $HOME/.mixplay/mixplay.conf
 */
void writeConfig( const char *musicpath ) {
	char	conffile[MAXPATHLEN+1]; /*  = "mixplay.conf"; */
	int		i;
	FILE	*fp;
	char *home=NULL;

	addMessage( 0, "Saving config" );
	assert( c_config != NULL );

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	if( musicpath != NULL ) {
		c_config->musicdir=(char*)falloc( strlen(musicpath)+1, sizeof( char ) );
		strip( c_config->musicdir, musicpath, strlen(musicpath)+1 );
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay", home );
	if( mkdir( conffile, 0700 ) == -1 ) {
		if( errno != EEXIST ) {
			fail( errno, "Could not create config dir %s", conffile );
		}
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/playlists", home );
	if( mkdir( conffile, 0700 ) == -1 ) {
		if( errno != EEXIST ) {
			fail( errno, "Could not create playlist dir %s", conffile );
		}
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home );

	fp=fopen( conffile, "w" );

	if( NULL != fp ) {
		fprintf( fp, "[mixplay]" );
		fprintf( fp, "\nmusicdir=%s", c_config->musicdir );
		if( c_config->profiles == 0 ) {
		fprintf( fp, "\nactive=1" );
			fprintf( fp, "\nprofiles=mixplay;" );
		}
		else {
			fprintf( fp, "\nactive=%i", c_config->active );
			fprintf( fp, "\nprofiles=" );
			for( i=0; i< c_config->profiles; i++ ) {
				fprintf( fp, "%s;", c_config->profile[i] );
			}
		}
		fprintf( fp, "\nstreams=" );
		for( i=0; i< c_config->streams; i++ ) {
			fprintf( fp, "%s;", c_config->stream[i] );
		}
		fprintf( fp, "\nsnames=" );
		for( i=0; i< c_config->streams; i++ ) {
			fprintf( fp, "%s;", c_config->sname[i] );
		}
		fprintf( fp, "\nskipdnp=%i", c_config->skipdnp );
		fprintf( fp, "\nfade=%i", c_config->fade );
		if( c_config->channel != NULL ) {
			fprintf( fp, "\nchannel=%s", c_config->channel );
		}
		else {
			fprintf( fp, "\nchannel=Master");
			fprintf( fp, "\n# channel=Digital for HifiBerry");
			fprintf( fp, "\n# channel=Main");
			fprintf( fp, "\n# channel=DAC");
		}
		fprintf( fp, "\nhost=%s", c_config->host );
		if( c_config->port != MP_PORT ) {
			fprintf( fp, "\nport=%i", c_config->port );
		}
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

	for( i=0; i<c_config->playlists; i++ ) {
		sfree( &(c_config->playlist[i]) );
	}
	c_config->playlists=0;
	sfree( (char **)&(c_config->playlist) );

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
	sfree( (char **)&(c_config->stream) );
	sfree( (char **)&(c_config->sname) );

	sfree( (char **)&(c_config->channel) );
	sfree( (char **)&(c_config->host) );

	msgBuffDiscard( c_config->msg );
}

/**
 * recursive free() to clean up all of the configuration
 */
void freeConfig( ) {
	assert( c_config != NULL );
	freeConfigContents( );
	c_config->current=wipePlaylist( c_config->current );
	c_config->found->titles=wipePlaylist( c_config->found->titles );
	if( c_config->found->artists != NULL ) {
		free( c_config->found->artists );
	}
	if( c_config->found->albums != NULL ) {
		free( c_config->found->albums );
	}
	if( c_config->found->albart != NULL ) {
		free( c_config->found->albart );
	}
	/* free root last so playlist cleanup does not double free titles */
	c_config->root=wipeTitles( c_config->root );
	free( c_config->found );
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
void addMessage( int v, const char *msg, ... ) {
	va_list args;
	char *line;

	pthread_mutex_lock( &msglock );
	line = (char*)falloc( MP_MSGLEN, 1 );
	va_start( args, msg );
	vsnprintf( line, MP_MSGLEN, msg, args );
	va_end( args );

	/* cut off trailing linefeeds */
	if( line[strlen(line)] == '\n' ) {
		line[strlen(line)] = 0;
	}

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

static int _ftrpos=0;
static char _curact[80]="";

/**
 * show activity roller on console
 * this will only show if debug mode is enabled
 */
void activity( const char *msg, ... ) {
	char roller[5]="|/-\\";
	int pos;
	va_list args;

	va_start( args, msg );
	vsnprintf( _curact, 80, msg, args );
	va_end( args );

	if( getDebug() ) {
		_ftrpos=( _ftrpos+1 )%( 400/getDebug() );
		if ( _ftrpos%( 100/getDebug() ) == 0 ) {
			pos=( _ftrpos/( 100/getDebug() ) )%4;
			printf( "%c %s                                  \r", roller[pos], _curact );
			fflush( stdout );
		}
	}
}

char *getCurrentActivity( void ) {
	return _curact;
}

static void addHook( void (*func)( void* ), _mpFunc **list ) {
	_mpFunc *pos=*list;
	if( pos == NULL ) {
		*list=(_mpFunc*)falloc(1,  sizeof(_mpFunc) );
		pos=*list;
	}
	else {
		while( pos->next != NULL ) {
			pos=pos->next;
		}
		pos->next=(_mpFunc*)falloc(1,  sizeof(_mpFunc) );
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
void progressStart( const char* msg, ... ) {
	va_list args;
	char *line;
	_mpFunc *pos=_pfunc;

	line=(char*)falloc( 512, 1 );

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end( args );

	while( pos != NULL ) {
		pos->func( line );
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

void progressMsg( const char *msg ) {
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
