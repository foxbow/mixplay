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

static pthread_mutex_t _addmsglock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _cblock=PTHREAD_MUTEX_INITIALIZER;
static mpconfig *c_config=NULL;

/**
 * the progress function list
 */
typedef struct _mpFunc_t _mpFunc;
struct _mpFunc_t {
	void (*func)( void * );
	void *arg;
	_mpFunc *next;
};

/* callback hooks */
static _mpFunc *_pfunc=NULL;
static _mpFunc *_ufunc=NULL;
static _mpFunc *_nfunc=NULL;

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
		"mpc_deldnp",
		"mpc_delfav",
		"mpc_remove",
		"mpc_mute",
		"mpc_favplay",
		"mpc_idle"
};

static void invokeHooks( _mpFunc *hooks ){
	_mpFunc *pos=hooks;
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
mpcmd mpcCommand( const char *name ) {
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
	mpconfig *control=getConfig();

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
	mpconfig *config=getConfig();
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
	mpconfig *control;
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
				fail( F_FAIL, "Config file Format error in %s!", line );
			}
		}
	}

	return num;
}

static int scanprofiles( char *input, struct profile_t ***target ) {
	char **line;
	int i, num;

	num=scanparts( input, &line );
	if( num > 0 ) {
		*target=(struct profile_t **)falloc( num, sizeof( struct profile_t *) );

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
	c_config->playcount=0;
	c_config->port=MP_PORT;
	c_config->changed=0;
	c_config->isDaemon=0;
	c_config->streamURL=NULL;

	snprintf( c_config->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", home );

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

				c_config->musicdir=(char*)falloc( strlen(pos)+1, 1 );
				strip( c_config->musicdir, pos, strlen(pos) );
			}
			if( strstr( line, "channel=" ) == line ) {
				c_config->channel=(char*)falloc( strlen(pos)+1, 1 );
				strip( c_config->channel, pos, strlen(pos) );
			}
			if( strstr( line, "profiles=" ) == line ) {
				c_config->profiles=scanprofiles( pos, &c_config->profile );
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
				if( c_config->active == 0 ) {
					addMessage(0,"Setting default profile!");
					c_config->active=1;
				}
			}
			if( strstr( line, "skipdnp=" ) == line ) {
				c_config->skipdnp=atoi(pos);
			}
			if( strstr( line, "fade=" ) == line ) {
				c_config->fade=atoi(pos);
			}
			if( strstr( line, "port=" ) == line ) {
				c_config->port=atoi(pos);
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
		c_config->musicdir=(char*)falloc( strlen(musicpath)+2, sizeof( char ) );
		strip( c_config->musicdir, musicpath, strlen(musicpath)+1 );
		if( c_config->musicdir[strlen(c_config->musicdir)] != '/' ) {
			strtcat( c_config->musicdir, "/", strlen(musicpath)+2 );
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
		fprintf( fp, "\nmusicdir=%s", c_config->musicdir );
		if( c_config->profiles == 0 ) {
		fprintf( fp, "\nactive=1" );
			fprintf( fp, "\nprofiles=mixplay;" );
		}
		else {
			fprintf( fp, "\nactive=%i", c_config->active );
			fprintf( fp, "\nprofiles=" );
			for( i=0; i< c_config->profiles; i++ ) {
				fprintf( fp, "%i:%s;", c_config->profile[i]->favplay,
					c_config->profile[i]->name );
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
	sfree( &(c_config->musicdir) );

	for( i=0; i<c_config->profiles; i++ ) {
		freeProfile( c_config->profile[i] );
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
	wipeList(c_config->dnplist);
	wipeList(c_config->favlist);

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

	pthread_mutex_lock( &_addmsglock );
	line = (char*)falloc( MP_MSGLEN+1, 1 );
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
				if( v < getDebug() ) {
					fprintf( stderr, "d%i %s\n", v, line );
				}
				/* not just a message but something important */
				if( v == -1 ) {
					memmove( line+6, line, MP_MSGLEN-6 );
					strncpy( line, "ALERT:", 6 );
					line[MP_MSGLEN]=0;
				}
				msgBuffAdd( c_config->msg, line );
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

static void addHook( void (*func)( void* ), void *arg, _mpFunc **list ) {
	_mpFunc *pos=*list;
	pthread_mutex_lock(&_cblock);
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
	pos->arg=arg;
	pos->next=NULL;
	pthread_mutex_unlock(&_cblock);
}

static void removeHook( void (*func)( void* ), void *arg, _mpFunc **list ) {
	_mpFunc *pos=*list;
	_mpFunc *pre=NULL;

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

struct profile_t *getProfile() {
	if( getConfig()->active < 1 ) {
		addMessage(0,"%i is not a valid profile!", getConfig()->active );
		return NULL;
	}
	return getConfig()->profile[getConfig()->active-1];
}

struct profile_t *createProfile( const char *name, const unsigned favplay ) {
	struct profile_t *profile =
		(struct profile_t *)falloc( 1, sizeof( struct profile_t ) );
	profile->name=falloc(strlen(name)+1,1);
	strcpy( profile->name, name );
	profile->favplay=favplay;
	return profile;
}

void freeProfile( struct profile_t *profile ){
	if( profile != NULL ) {
		if( profile->name != NULL ) {
			free( profile->name );
		}
		free( profile );
	}
}
