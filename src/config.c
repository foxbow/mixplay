/*
 * config.c
 *
 * handles reading and writing the configuration
 *
 * the configuration file looks like a standard GTK configuration for
 * historical reasons. It should also parse with the gtk_* functions but those
 * are not always available in headless environments.
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

#include "musicmgr.h"
#include "config.h"
#include "mpcomm.h"
#include "player.h"
#include "mpserver.h"

static pthread_mutex_t _addmsglock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _cblock=PTHREAD_MUTEX_INITIALIZER;
static mpconfig_t *_cconfig=NULL;

/**
 * the progress function list
 */
typedef struct _mpfunc_t _mpfunc;
struct _mpfunc_t {
	void (*func)( void * );
	void *arg;
	_mpfunc *next;
};

/* callback hooks */
static _mpfunc *_pfunc=NULL;
static _mpfunc *_ufunc=NULL;
static _mpfunc *_nfunc=NULL;

static const char *mpccommand[] = {
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
		"mpc_move",
		"mpc_dbinfo",
		"mpc_search",
		"mpc_append",
		"mpc_setvol",
		"mpc_newprof",
		"mpc_path",
		"mpc_remprof",
		"mpc_edit",
		"mpc_deldnp",
		"mpc_delfav",
		"mpc_remove",
		"mpc_mute",
		"mpc_favplay",
		"mpc_idle"
};

static void invokeHooks( _mpfunc *hooks ){
	_mpfunc *pos=hooks;
	pthread_mutex_lock(&_cblock);
	while( pos != NULL ) {
		pos->func(pos->arg);
		pos=pos->next;
	}
	pthread_mutex_unlock(&_cblock);
}

/*
 * transform an mpcmd value into a string literal
 */
const char *mpcString( mpcmd_t rawcmd ) {
	mpcmd_t cmd=MPC_CMD(rawcmd);
	if( cmd <= mpc_idle ) {
		return mpccommand[cmd];
	}
	else {
		addMessage( 1, "Unknown command code %i", cmd );
		return "mpc_idle";
	}
}

/*
 * transform a string literal into an mpcmd value
 */
mpcmd_t mpcCommand( const char *name ) {
	int i;
	for( i=0; i<= mpc_idle; i++ ) {
		if( strstr( name, mpccommand[i] ) ) break;
	}
	if( i>mpc_idle ) {
		addMessage( 1, "Unknown command %s!", name );
		return mpc_idle;
	}
	return (mpcmd_t)i;
}

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
		control->mpmode=PM_STREAM;
		setStream( line, "Waiting for stream info..." );
		return 1;
	}
	else if( endsWith( arg, ".mp3" ) ) {
		addMessage( 1, "Single file: %s", arg );
		/* play single song... */
		control->mpmode=PM_PLAYLIST;
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
			control->mpmode=PM_PLAYLIST;
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
	/* todo: support database playlists */
	else if ( endsWith( arg, ".m3u" ) ||
			  endsWith( arg, ".pls" ) ) {
		addMessage( 1, "Playlist: %s", arg );
		control->mpmode=PM_PLAYLIST;
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
	mpconfig_t *control;
	pthread_t tid;
	struct timespec ts;
	control=getConfig();
	ts.tv_sec=0;
	ts.tv_nsec=250000;

	/* start the comm server */
	pthread_create( &control->stid, NULL, mpserver, NULL );
	nanosleep(&ts, NULL);

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
		setCommand( mpc_play );
	}

	return 0;
}

/**
 * returns the current configuration
 */
mpconfig_t *getConfig() {
	assert( _cconfig != NULL );
	return _cconfig;
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
				fail( F_FAIL, "Config file Format error in %s!", line );
			}
		}
	}

	return num;
}

static int scanprofiles( char *input, profile_t ***target ) {
	char **line;
	int i, num;

	num=scanparts( input, &line );
	if( num > 0 ) {
		*target=(profile_t **)falloc( num, sizeof( profile_t *) );

		for( i=0; i<num; i++ ) {
			if( line[i][1] == ':' ) {
				(*target)[i]=createProfile( line[i]+2, 0 );
				if (line[i][0]=='1') {
					(*target)[i]->favplay=1;
				}

			}
			else {
				(*target)[i]=createProfile( line[i], 0 );
			}
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
mpconfig_t *readConfig( void ) {
	char	conffile[MAXPATHLEN+1]; /*  = "mixplay.conf"; */
	char	line[MAXPATHLEN+1];
	char	*pos;
	char	*home=NULL;
	FILE	*fp;

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	if( _cconfig == NULL ) {
		_cconfig=(mpconfig_t*)falloc( 1, sizeof( mpconfig_t ) );
		_cconfig->msg=msgBuffInit();
		_cconfig->found=(searchresults_t*)falloc(1,sizeof(searchresults_t));
	}

	/* Set some default values */
	_cconfig->root=NULL;
	_cconfig->current=NULL;
	_cconfig->volume=80;
	strcpy( _cconfig->playtime, "00:00" );
	strcpy( _cconfig->remtime, "00:00" );
	_cconfig->percent=0;
	_cconfig->status=mpc_idle;
	_cconfig->command=mpc_idle;
	_cconfig->dbname=(char*)falloc( MAXPATHLEN+1, 1 );
	_cconfig->verbosity=0;
	_cconfig->debug=0;
	_cconfig->fade=1;
	_cconfig->inUI=0;
	_cconfig->msg->lines=0;
	_cconfig->msg->current=0;
	_cconfig->port=MP_PORT;
	_cconfig->changed=0;
	_cconfig->isDaemon=0;
	_cconfig->streamURL=NULL;

	snprintf( _cconfig->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", home );

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home );
	fp=fopen( conffile, "r" );

	if( NULL != fp ) {
		do {
			if( fgets( line, MAXPATHLEN, fp ) == NULL ) {
				continue;
			}
			if( line[0]=='#' ) continue;
			pos=strchr( line, '=' );
			if( ( NULL == pos ) || ( strlen( ++pos ) == 0 ) ) continue;
			/* cut off trailing \n */
			line[strlen(line)-1]=0;

			if( strstr( line, "musicdir=" ) == line ) {
				/* make sure that musicdir ends with a '/' */
				if( line[strlen(line)-1] != '/' ) {
					line[strlen(line)] = '/';
				}

				_cconfig->musicdir=(char*)falloc( strlen(pos)+1, 1 );
				strip( _cconfig->musicdir, pos, strlen(pos) );
			}
			if( strstr( line, "channel=" ) == line ) {
				_cconfig->channel=(char*)falloc( strlen(pos)+1, 1 );
				strip( _cconfig->channel, pos, strlen(pos) );
			}
			if( strstr( line, "profiles=" ) == line ) {
				_cconfig->profiles=scanprofiles( pos, &_cconfig->profile );
			}
			if( strstr( line, "streams=" ) == line ) {
				_cconfig->streams=scanparts( pos, &_cconfig->stream );
			}
			if( strstr( line, "snames=" ) == line ) {
				if( scanparts( pos, &_cconfig->sname ) != _cconfig->streams ) {
					fclose(fp);
					fail( F_FAIL, "Number of streams and stream names does not match!");
				}
			}
			if( strstr( line, "active=" ) == line ) {
				_cconfig->active=atoi(pos);
				if( _cconfig->active == 0 ) {
					addMessage(0,"Setting default profile!");
					_cconfig->active=1;
				}
			}
			if( strstr( line, "skipdnp=" ) == line ) {
				_cconfig->skipdnp=atoi(pos);
			}
			if( strstr( line, "fade=" ) == line ) {
				_cconfig->fade=atoi(pos);
			}
			if( strstr( line, "port=" ) == line ) {
				_cconfig->port=atoi(pos);
			}
		}
		while( !feof( fp ) );

		fclose(fp);

		return _cconfig;
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
	assert( _cconfig != NULL );

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	if( musicpath != NULL ) {
		_cconfig->musicdir=(char*)falloc( strlen(musicpath)+2, sizeof( char ) );
		strip( _cconfig->musicdir, musicpath, strlen(musicpath)+1 );
		if( _cconfig->musicdir[strlen(_cconfig->musicdir)] != '/' ) {
			strtcat( _cconfig->musicdir, "/", strlen(musicpath)+2 );
		}
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay", home );
	if( mkdir( conffile, 0700 ) == -1 ) {
		if( errno != EEXIST ) {
			fail( errno, "Could not create config dir %s", conffile );
		}
	}

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home );

	fp=fopen( conffile, "w" );

	if( NULL != fp ) {
		fprintf( fp, "[mixplay]" );
		fprintf( fp, "\nmusicdir=%s", _cconfig->musicdir );
		if( _cconfig->profiles == 0 ) {
		fprintf( fp, "\nactive=1" );
			fprintf( fp, "\nprofiles=mixplay;" );
		}
		else {
			fprintf( fp, "\nactive=%i", _cconfig->active );
			fprintf( fp, "\nprofiles=" );
			for( i=0; i< _cconfig->profiles; i++ ) {
				fprintf( fp, "%i:%s;", _cconfig->profile[i]->favplay,
					_cconfig->profile[i]->name );
			}
		}
		fprintf( fp, "\nstreams=" );
		for( i=0; i< _cconfig->streams; i++ ) {
			fprintf( fp, "%s;", _cconfig->stream[i] );
		}
		fprintf( fp, "\nsnames=" );
		for( i=0; i< _cconfig->streams; i++ ) {
			fprintf( fp, "%s;", _cconfig->sname[i] );
		}
		fprintf( fp, "\nskipdnp=%i", _cconfig->skipdnp );
		fprintf( fp, "\nfade=%i", _cconfig->fade );
		if( _cconfig->channel != NULL ) {
			fprintf( fp, "\nchannel=%s", _cconfig->channel );
		}
		else {
			fprintf( fp, "\nchannel=Master");
			fprintf( fp, "\n# channel=Digital for HifiBerry");
			fprintf( fp, "\n# channel=Main");
			fprintf( fp, "\n# channel=DAC");
		}
		if( _cconfig->port != MP_PORT ) {
			fprintf( fp, "\nport=%i", _cconfig->port );
		}
		fprintf( fp, "\n" );
		fclose( fp );
		_cconfig->changed=0;
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
	assert( _cconfig != NULL );

	sfree( &(_cconfig->dbname) );
	sfree( &(_cconfig->musicdir) );

	for( i=0; i<_cconfig->profiles; i++ ) {
		freeProfile( _cconfig->profile[i] );
	}
	_cconfig->profiles=0;
	sfree( (char **)&(_cconfig->profile) );

	for( i=0; i<_cconfig->streams; i++ ) {
		sfree( &(_cconfig->stream[i]) );
		sfree( &(_cconfig->sname[i]) );
	}
	_cconfig->streams=0;
	sfree( (char **)&(_cconfig->stream) );
	sfree( (char **)&(_cconfig->sname) );

	sfree( (char **)&(_cconfig->channel) );

	msgBuffDiscard( _cconfig->msg );
}

/**
 * recursive free() to clean up all of the configuration
 */
void freeConfig( ) {
	assert( _cconfig != NULL );
	freeConfigContents( );
	_cconfig->current=wipePlaylist( _cconfig->current );
	_cconfig->found->titles=wipePlaylist( _cconfig->found->titles );
	if( _cconfig->found->artists != NULL ) {
		free( _cconfig->found->artists );
	}
	if( _cconfig->found->albums != NULL ) {
		free( _cconfig->found->albums );
	}
	if( _cconfig->found->albart != NULL ) {
		free( _cconfig->found->albart );
	}
	/* free root last so playlist cleanup does not double free titles */
	_cconfig->root=wipeTitles( _cconfig->root );
	free( _cconfig->found );
	wipeList(_cconfig->dnplist);
	wipeList(_cconfig->favlist);

	free( _cconfig );
	_cconfig=NULL;
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

	pthread_mutex_lock( &_addmsglock );
	line = (char*)falloc( MP_MSGLEN+1, 1 );
	va_start( args, msg );
	vsnprintf( line, MP_MSGLEN, msg, args );
	va_end( args );

	/* cut off trailing linefeeds */
	if( line[strlen(line)] == '\n' ) {
		line[strlen(line)] = 0;
	}

	if( _cconfig == NULL ) {
		fprintf( stderr, "* %s\n", line );
	}
	else {
		if( v <= getVerbosity() ) {
			if( _cconfig->inUI ) {
				if( v < getDebug() ) {
					fprintf( stderr, "d%i %s\n", v, line );
				}
				/* not just a message but something important */
				if( v == -1 ) {
					memmove( line+6, line, MP_MSGLEN-6 );
					strncpy( line, "ALERT:", 6 );
					line[MP_MSGLEN]=0;
				}
				msgBuffAdd( _cconfig->msg, line );
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
	pthread_mutex_unlock( &_addmsglock );
}

/**
 * gets the current message removes it from the ring
 */
char *getMessage() {
	return msgBuffGet( _cconfig->msg );
}

void incDebug( void ) {
	assert( _cconfig != NULL );
	_cconfig->debug++;
}

int getDebug( void ) {
	assert( _cconfig != NULL );
	return _cconfig->debug;
}

int setVerbosity( int v ) {
	assert( _cconfig != NULL );
	_cconfig->verbosity=v;
	return _cconfig->verbosity;
}

int getVerbosity( void ) {
	assert( _cconfig != NULL );
	return _cconfig->verbosity;
}

int incVerbosity() {
	assert( _cconfig != NULL );
	_cconfig->verbosity++;
	return _cconfig->verbosity;
}

void muteVerbosity() {
	setVerbosity(0);
}

static int _ftrpos=0;
#define MP_ACTLEN 75
static char _curact[MP_ACTLEN]="";

/**
 * show activity roller on console
 * this will only show if debug mode is enabled
 */
void activity( const char *msg, ... ) {
	char roller[5]="|/-\\";
	int pos;
	va_list args;

	va_start( args, msg );
	vsnprintf( _curact, MP_ACTLEN, msg, args );
	va_end( args );

	for( pos=strlen(_curact); pos < (MP_ACTLEN-1); pos++ ){
		_curact[pos]=' ';
	}
	_curact[MP_ACTLEN-1]=0;

	if( getDebug() ) {
		_ftrpos=( _ftrpos+1 )%( 400/getDebug() );
		if ( _ftrpos%( 100/getDebug() ) == 0 ) {
			pos=( _ftrpos/( 100/getDebug() ) )%4;
			printf( "%c %s\r", roller[pos], _curact );
			fflush( stdout );
		}
	}
}

char *getCurrentActivity( void ) {
	return _curact;
}

static void addHook( void (*func)( void* ), void *arg, _mpfunc **list ) {
	_mpfunc *pos=*list;
	pthread_mutex_lock(&_cblock);
	if( pos == NULL ) {
		*list=(_mpfunc*)falloc(1,  sizeof(_mpfunc) );
		pos=*list;
	}
	else {
		while( pos->next != NULL ) {
			pos=pos->next;
		}
		pos->next=(_mpfunc*)falloc(1,  sizeof(_mpfunc) );
		pos=pos->next;
	}
	pos->func=func;
	pos->arg=arg;
	pos->next=NULL;
	pthread_mutex_unlock(&_cblock);
}

static void removeHook( void (*func)( void* ), void *arg, _mpfunc **list ) {
	_mpfunc *pos=*list;
	_mpfunc *pre=NULL;

	pthread_mutex_lock(&_cblock);

	if( pos == NULL ) {
		addMessage( 0 ,"Empty callback list!" );
		return;
	}

	/* does the callback to be removed lead the list? */
	if( ( pos->func == func ) && ( pos->arg == arg ) ){
		*list=pos->next;
		free( pos );
	}
	/* step through the rest of the list */
	else {
		while( pos->next != NULL ) {
			pre=pos;
			pos=pos->next;
			if( ( pos->func == func ) && ( pos->arg == arg ) ){
				pre->next=pos->next;
				free( pos );
				break;
			}
			else {
				pos=pos->next;
			}
		}
	}

	pthread_mutex_unlock(&_cblock);
}

/*
 * stub implementations
 */
void progressStart( const char* msg, ... ) {
	va_list args;
	char *line;

	line=(char*)falloc( 512, 1 );

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end( args );

	addMessage( 0, "%s", line );
	invokeHooks(_pfunc);

	free( line );
}

/**
 * end a progress display
 */
void progressEnd( void ) {
	addMessage( 0, "Done." );
	invokeHooks(_pfunc);
}

/*
 * a single line that is sent to the current client
 * this should handled as an alert (e.g.: PopUp) on the client
 */
void progressMsg( const char *msg ) {
	progressStart( "ALERT:%s", msg );
	progressEnd();
}

/**
 * register a progress function.
 * Not used yet, may replace setCurrentClient()
 */
void addProgressHook( void (*func)( void * ) ){
	addHook( func, NULL, &_pfunc );
}

/**
 * register an update function, called on minor updates like playtime
 */
 void addUpdateHook( void (*func)( void * ) ){
 	addHook( func, NULL, &_ufunc );
 }

/**
 * register a notification function, called when title/playlist has changed
 */
void addNotifyHook( void (*func)( void * ), void *arg ){
	addHook( func, arg, &_nfunc );
}

void removeNotifyHook( void (*func)( void * ), void *arg ) {
	removeHook( func, arg, &_nfunc );
}

/**
 * notify all clients that the title info has unchanged
 */
void notifyChange() {
	invokeHooks(_nfunc);
}

/**
 * run all registered update functions
 */
void updateUI() {
	invokeHooks(_ufunc);
}

/*
 * returns a pointer to a string containing a full absolute path to the file
 */
char *fullpath( const char *file ) {
	static char pbuff[MAXPATHLEN+1];
	pbuff[0]=0;
	if( file[0] != '/' ) {
		strtcpy( pbuff, getConfig()->musicdir, MAXPATHLEN );
		strtcat( pbuff, file, MAXPATHLEN );
	}
	else {
		strtcpy( pbuff, file, MAXPATHLEN );
	}
	return pbuff;
}

profile_t *getProfile() {
	if( getConfig()->active < 1 ) {
		addMessage(0,"%i is not a valid profile!", getConfig()->active );
		return NULL;
	}
	return getConfig()->profile[getConfig()->active-1];
}

profile_t *createProfile( const char *name, const unsigned favplay ) {
	profile_t *profile =
		(profile_t *)falloc( 1, sizeof( profile_t ) );
	profile->name=falloc(strlen(name)+1,1);
	strcpy( profile->name, name );
	profile->favplay=favplay;
	return profile;
}

void freeProfile( profile_t *profile ){
	if( profile != NULL ) {
		if( profile->name != NULL ) {
			free( profile->name );
		}
		free( profile );
	}
}
