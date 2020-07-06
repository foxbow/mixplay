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
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

#include "utils.h"
#include "config.h"
#include "musicmgr.h"
#include "mpcomm.h"

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
static _mpfunc *_ufunc=NULL;

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
			free(line[i]);
		}
		free(line);
	}
	return num;
}

static int scancodes( char *input, int *codes ) {
	char **line;
	int i, num;

	num=scanparts( input, &line );
	if( num > 0 ) {
		if( num == MPRC_NUM ) {
			for( i=0; i < MPRC_NUM; i++ ) {
				codes[i]=atoi(line[i]);
			}
		}
		for( i=0; i < num; i++ ) {
			free(line[i]);
		}
		free(line);
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
	char	*line;
	char	*pos;
	char	*home=NULL;
	FILE	*fp;
	int		i;

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
	_cconfig->playtime=0;
	_cconfig->remtime=0;
	_cconfig->percent=0;
	_cconfig->status=mpc_idle;
	_cconfig->command=mpc_idle;
	_cconfig->dbname=(char*)falloc( MAXPATHLEN+1, 1 );
	_cconfig->password=strdup("mixplay");
	_cconfig->verbosity=0;
	_cconfig->skipdnp=3;
	_cconfig->sleepto=0;
	_cconfig->debug=0;
	_cconfig->fade=1;
	_cconfig->inUI=0;
	_cconfig->msg->lines=0;
	_cconfig->msg->current=0;
	_cconfig->port=MP_PORT;
	_cconfig->changed=0;
	_cconfig->isDaemon=0;
	_cconfig->fpcurrent=1;
	_cconfig->streamURL=NULL;
	_cconfig->rcdev=NULL;
	_cconfig->mpmode=PM_NONE;
	for (i=0; i<MAXCLIENT; i++) _cconfig->client[i]=0;
	for (i=0; i<MAXCLIENT; i++) _cconfig->notify[i]=MPCOMM_STAT;
	pthread_mutex_init ( &(_cconfig->pllock), NULL );

	snprintf( _cconfig->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", home );

	snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home );
	fp=fopen( conffile, "r" );

	if( NULL != fp ) {
		do {
			if( (line=fetchline(fp) ) == NULL ) {
				continue;
			}
			if( line[0]=='#' ) {
				free(line);
				continue;
			}
			pos=strchr( line, '=' );
			if( ( NULL == pos ) || ( strlen( ++pos ) == 0 ) ) {
				free(line);
				continue;
			}

			if( strstr( line, "musicdir=" ) == line ) {
				/* make sure that musicdir ends with a '/' */
				if( line[strlen(line)-1] != '/' ) {
					strtcat( line, "/", strlen(line)+1 );
				}

				_cconfig->musicdir=strdup(pos);
				strip( _cconfig->musicdir, pos, strlen(pos) );
			}
			if( strstr( line, "password=" ) == line ) {
				/* make sure that musicdir ends with a '/' */
				_cconfig->password=(char*)frealloc( _cconfig->password, strlen(pos)+1 );
				strip( _cconfig->password, pos, strlen(pos) );
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
					addMessage( 0, "Setting default profile!");
					_cconfig->active=1;
				}
			}
			if( strstr( line, "skipdnp=" ) == line ) {
				_cconfig->skipdnp=atoi(pos);
			}
			if( strstr( line, "sleepto=" ) == line ) {
				_cconfig->sleepto=atoi(pos);
			}
			if( strstr( line, "fade=" ) == line ) {
				_cconfig->fade=atoi(pos);
			}
			if( strstr( line, "port=" ) == line ) {
				_cconfig->port=atoi(pos);
			}
			if( strstr( line, "rcdev=" ) == line ) {
				_cconfig->rcdev=(char*)falloc( strlen(pos)+1, 1 );
				strip( _cconfig->rcdev, pos, strlen(pos) );
			}
			if( strstr( line, "rccodes=") == line ) {
				if( scancodes( pos, _cconfig->rccodes ) != MPRC_NUM ) {
					fclose(fp);
					fail( F_FAIL, "Wrong number of RC codes!");
				}
			}
			free(line);
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

	addMessage( 1, "Saving config" );
	assert( _cconfig != NULL );

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	if( musicpath != NULL ) {
		_cconfig->musicdir=(char*)falloc( strlen(musicpath)+2, sizeof( char ) );
		strip( _cconfig->musicdir, musicpath, strlen(musicpath)+1 );
		if( _cconfig->musicdir[strlen(_cconfig->musicdir)-1] != '/' ) {
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
		fprintf( fp, "\npassword=%s", _cconfig->password );
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
		fprintf( fp, "\nsleepto=%i", _cconfig->sleepto );
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
		if( _cconfig->rcdev != NULL ) {
			fprintf( fp, "\nrcdev=%s", _cconfig->rcdev );
			fprintf( fp, "\nrccodes=");
			for( i=0; i<MPRC_NUM; i++ ) {
				fprintf( fp, "%i;", _cconfig->rccodes[i] );
			}
		}
		fprintf( fp, "\n" );
		fclose( fp );
		_cconfig->changed=0;
	}
	else {
		fail( errno, "Could not open %s", conffile );
	}
}

mpconfig_t *createConfig() {
	char path[MAXPATHLEN];

	printf( "music directory needs to be set.\n" );
	printf( "It will be set up now\n" );
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
	return readConfig();
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

	sfree( (char **)&(_cconfig->password) );

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
 * If the application is not in UI mode, the message will just be printed to
 * make sure messages are displayed on the correct media.
 *
 * If debug > v the message is printed on the console (to avoid verbosity
 * 0 messages to always appear in the debug stream.
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
					fprintf( stderr, "\r%2i/%2i %s\n", v, getCurClient(), line );
				}
				/* not just a message but something important so add the ALERT:
				   prefix for the clients */
				if( v == -1 ) {
					memmove( line+6, line, MP_MSGLEN-6 );
					memcpy( line, "ALERT:", 6 );
					line[MP_MSGLEN]=0;
				}
				msgBuffAdd( _cconfig->msg, line );
			}
			else if (getCurClient() == -1) {
				printf( "\r%s\n", line );
			}
		}
		else if( v < getDebug() ) {
			fprintf( stderr, "\rD%i %s\n", v, line );
		}
	}

	free(line);
	pthread_mutex_unlock( &_addmsglock );
}

void incDebug( void ) {
	assert( _cconfig != NULL );
	_cconfig->debug++;
}

int getDebug( void ) {
	assert( _cconfig != NULL );
	return _cconfig->debug;
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

/* returns true if the config is in a well defined state */
int playerIsInactive( void ) {
	mpconfig_t *control=getConfig();
	return( (control->status == mpc_start) ||
					(control->command == mpc_start) ||
					(control->mpmode & PM_SWITCH) );
}

static unsigned _ftrpos=0;
#define MP_ACTLEN 75
static char _curact[MP_ACTLEN]="";

/**
 * show activity roller on console
 * this will only show if debug mode is enabled
 */
void activity( int v, const char *msg, ... ) {
	char roller[5]="|/-\\";
	int pos=0;
	int i;
	va_list args;
	char newact[MP_ACTLEN]="";

	va_start( args, msg );
	vsnprintf( newact, MP_ACTLEN, msg, args );
	va_end( args );

	for( i=strlen(newact); i < (MP_ACTLEN-1); i++ ){
		newact[i]=' ';
	}
	_curact[MP_ACTLEN-1]=0;
	/* _ftrpos is unsigned so an overflow does not cause issues later */
	++_ftrpos;

	/* add a message for dbinfo et.al. */
	if(strcmp(newact, _curact)) {
		strcpy(_curact, newact);
		/* All notifications so far sucked one way or the other.. */
		if( playerIsInactive() ) {
			addMessage(v, "%s", newact);
		}
	}

	if ( ( v < getDebug() ) && ( _ftrpos % 100 == 0 ) ){
		printf( "%c %s\r", roller[pos], newact );
		fflush( stdout );
		pos=(pos+1)%4;
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

#if 0
static void removeHook( void (*func)( void* ), void *arg, _mpfunc **list ) {
	_mpfunc *pos=*list;
	_mpfunc *pre=NULL;

	pthread_mutex_lock(&_cblock);

	if( pos == NULL ) {
		addMessage( 0 , "Empty callback list!" );
		pthread_mutex_unlock(&_cblock);
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
		}
	}

	pthread_mutex_unlock(&_cblock);
}
#endif

/**
 * register an update function, called on minor updates like playtime
 */
void addUpdateHook( void (*func)( ) ){
	addHook( func, NULL, &_ufunc );
}

/**
 * notify all clients that a bigger update is needed
 */
void notifyChange(int state) {
	int i;
	pthread_mutex_lock(&_cblock);
	for (i=0;i<MAXCLIENT;i++) {
		getConfig()->notify[i] |= state;
	}
	pthread_mutex_unlock(&_cblock);
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

int getFavplay() {
	if( getConfig()->active > 0 ) {
		return getConfig()->profile[getConfig()->active-1]->favplay;
	}
	return 0;
}

int toggleFavplay() {
	profile_t *profile;
	if( getConfig()->active > 0 ) {
		profile=getConfig()->profile[getConfig()->active-1];
		profile->favplay=!profile->favplay;
		return profile->favplay;
	}
	return 0;
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
/**
 * deletes the current playlist
 * this is not in musicmanager.c to keep cross-dependecies in
 * config.c low
 */
mpplaylist_t *wipePlaylist( mpplaylist_t *pl ) {
	mpplaylist_t *next=NULL;

	if( pl == NULL ) {
		return NULL;
	}

	while( pl->prev != NULL ) {
		pl=pl->prev;
	}

	pthread_mutex_lock( &(getConfig()->pllock) );
	while( pl != NULL ){
		next=pl->next;
		if( getConfig()->root == NULL ) {
			free( pl->title );
		}
		free(pl);
		pl=next;
	}
	pthread_mutex_unlock( &(getConfig()->pllock) );

	return NULL;
}

/**
 * discards a list of markterms and frees the memory
 * returns NULL for intuitive calling
 */
marklist_t *wipeList( marklist_t *root ) {
	marklist_t *runner=root;

	if( root == NULL ) {
		return NULL;
	}

	pthread_mutex_lock( &(getConfig()->pllock) );
	if( NULL != root ) {
		while( runner != NULL ) {
			root=runner->next;
			free( runner );
			runner=root;
		}
	}
	pthread_mutex_unlock( &(getConfig()->pllock) );

	return NULL;
}

/**
 * discards a list of titles and frees the memory
 * returns NULL for intuitive calling
 */
mptitle_t *wipeTitles( mptitle_t *root ) {
	mptitle_t *runner=root;

	if( NULL != root ) {
		root->prev->next=NULL;

		while( runner != NULL ) {
			activity( 1, "Cleaning" );
			root=runner->next;
			free( runner );
			runner=root;
		}
	}

	return NULL;
}

/*
 * block all signals that would interrupt the execution flow
 * Yes, this is bad practice, yes this will become a signal handler thread
 */
void blockSigint() {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	if( pthread_sigmask(SIG_BLOCK, &set, NULL) != 0 ) {
		addMessage( 0, "Could not block SIGINT" );
	}
}

int checkPasswd() {
	if( getConfig()->argument != NULL ) {
		if( strcmp( getConfig()->password, getConfig()->argument ) == 0 ) {
			return 1;
		}
	}
	addMessage( -1, "Wrong password!" );
	return 0;
}

int getFreeClient( void ) {
	int i;
	for (i=0; i<MAXCLIENT; i++) {
		if (getConfig()->client[i] == 0) {
			getConfig()->client[i]=1;
			return i+1;
		}
	}
	addMessage(-1, "Out ofd clients!");
	return -1;
}

void freeClient( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->client[client]=0;
	}
}

int trylockClient( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		if( getConfig()->client[client] == 0 ) {
			getConfig()->client[client]=1;
			return 1;
		}
		return 0;
	}
	addMessage(1, "Client id %i out of range!", client+1);
	return -1;
}

int getNotify( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		return getConfig()->notify[client];
	}
	return -1;
}

void setNotify( int client, int state ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->notify[client]=state;
	}
}

void addNotify( int client, int state ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->notify[client]|=state;
	}
}

unsigned long getMsgCnt( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		return getConfig()->msgcnt[client];
	}
	return 0;
}

void setMsgCnt( int client, unsigned long count ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->msgcnt[client]=count;
	}
}

void incMsgCnt( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->msgcnt[client]++;
	}
}

void initMsgCnt( int client ) {
	client--;
	if( (client >= 0) && (client < MAXCLIENT) ) {
		getConfig()->msgcnt[client]=msgBufGetLastRead(getConfig()->msg);
	}
}
