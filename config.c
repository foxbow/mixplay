/*
 * config.c
 *
 * handles reading and writing the configuration
 *
 * the configuration file mooks like a standard GTK configuration for historical reasons.
 * It should also parse with the gtk_* functions but those are not always avalable in headless
 * environments.
 *
 *  Created on: 16.11.2017
 *      Author: bweber
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>

#include "utils.h"
#include "musicmgr.h"
#include "config.h"
#include "mpcomm.h"
#include "player.h"

static pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;
static mpconfig *c_config=NULL;

static const char *mpc_command[] = {
	    "mpc_play",
	    "mpc_stop",
	    "mpc_prev",
	    "mpc_next",
	    "mpc_start",
	    "mpc_favtitle",
	    "mpc_favartist",
	    "mpc_favalbum",
	    "mpc_repl",
	    "mpc_profile",
	    "mpc_quit",
	    "mpc_dbclean",
	    "mpc_dnptitle",
	    "mpc_dnpartist",
	    "mpc_dnpalbum",
	    "mpc_dnpgenre",
		"mpc_doublets",
		"mpc_shuffle",
		"mpc_ivol",
		"mpc_dvol",
		"mpc_bskip",
		"mpc_fskip",
		"mpc_QUIT",
	    "mpc_idle"
};

const char *mpcString( mpcmd cmd ) {
	return mpc_command[cmd];
}

const mpcmd mpcCommand( const char *name ) {
	int i=0;
	for( i=0; i<= mpc_idle; i++ ) {
		if( strstr( name, mpc_command[i] ) ) break;
	}
	if( i>mpc_idle ) {
		fail( F_FAIL, "Unknown command code %i for >%s<!", i, name );
	}
	return i;
}

void setCommand( mpcmd cmd ) {
	mpconfig *config;
	config=getConfig();

	if( cmd == mpc_idle ) {
		return;
	}

	if( config->remote ) {
		switch( cmd ) {
		case mpc_QUIT: /* also quit server! */
			setSCommand( mpc_quit );
			/* no break */
		case mpc_quit:
			config->status = mpc_quit;
			break;
		default:
			setSCommand( cmd );
		}
	}
	else {
		setPCommand( cmd );
	}
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
    FILE    *fp;

    if( c_config == NULL ) {
    	c_config=falloc( 1, sizeof( mpconfig ) );
    	c_config->msg=falloc( 1, sizeof( struct msgbuf_t ) );
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
    	return c_config;
    }

   	return NULL;
}

/**
 * writes the configuration from the given control structure into the file at
 * $HOME/.mixplay/mixplay.conf
 */
mpconfig *writeConfig( const char *musicpath ) {
    char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    int		i;
    FILE    *fp;

    addMessage( 1, "Writing config" );
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
    }
    else {
        fail( errno, "Could not open %s", conffile );
    }

    return c_config;
}

/**
 * just free something if it actually exists
 */
static void sfree( void *ptr ) {
	if( ptr != NULL ) {
		free( ptr );
	}
}

/**
 * recursive free() to clean up all of the configuration
 */
void freeConfig( ) {
	int i;

	assert( c_config != NULL );

    sfree( c_config->dbname );
    sfree( c_config->dnpname );
    sfree( c_config->favname );
    sfree( c_config->musicdir );
    for( i=0; i<c_config->profiles; i++ ) {
    	sfree( c_config->profile[i] );
    }
    sfree( c_config->profile );

    for( i=0; i<c_config->streams; i++ ) {
    	sfree( c_config->stream[i] );
    	sfree( c_config->sname[i] );
    }
    sfree( c_config->stream );
    sfree( c_config->sname );

    c_config->root=cleanTitles( c_config->root );
    sfree( c_config );
    c_config=NULL;
}

/**
 * adds a message to the message buffer
 *
 * If the application is not in UI mode, the message will just be printed to make sure messages
 * are displayed on the correct media.
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
		fprintf( stderr, "*%i %s\n", v, line );
		free(line);
    	pthread_mutex_unlock( &msglock );
		return;
    }

	if( v <= getVerbosity() ) {
    	if( c_config->inUI ) {
			msgBuffAdd( c_config->msg, line );
			if( v < getDebug() ) {
				fprintf( stderr, "D%i %s\n", v, line );
			}
    	}
    	else {
    		printf( "V%i %s\n", v, line );
    		free(line);
    	}
    }
	else if( v < getDebug() ) {
		fprintf( stderr, "D%i %s\n", v, line );
		free( line );
    }
	pthread_mutex_unlock( &msglock );
}

/**
 * gets the current message removes it from the ring and frees the message buffer
 * returns 0 and makes msg an empty string if no message is in the ring
 */
int getMessage( char *msg ) {
	char *buf;

	assert( c_config != NULL );

	pthread_mutex_lock( &msglock );
	buf=msgBuffGet( c_config->msg );
	if( buf != NULL ) {
		strcpy( msg, buf );
		free(buf);
	}
	else {
		msg[0]=0;
	}
	pthread_mutex_unlock( &msglock );

	return strlen( msg );
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

