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

#include "utils.h"
#include "musicmgr.h"
#include "config.h"

static pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;
static mpconfig *c_config;

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
 * returns -1 is no configuration file exists
 */
int readConfig( mpconfig *config ) {
    char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    char	line[MAXPATHLEN];
    char	*pos;
    FILE    *fp;

    assert( config != NULL );
    c_config=config;

    addMessage( 1, "Reading config\n" );

    /* Set some default values */
    config->root=NULL;
    config->current=config->root;
    config->playstream=0;
    config->volume=80;
    strcpy( config->playtime, "00:00" );
    strcpy( config->remtime, "00:00" );
    config->percent=0;
    config->status=mpc_idle;
    config->command=mpc_idle;
    config->dbname=falloc( MAXPATHLEN, sizeof( char ) );
    config->msg.lines=0;
    config->msg.current=0;
    snprintf( config->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", getenv("HOME") );

    snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );
	fp=fopen( conffile, "r" );

    if( NULL != fp ) {
        do {
            fgets( line, MAXPATHLEN, fp );
            if( line[0]=='#' ) continue;
            pos=strchr( line, '=' );
            if( ( NULL == pos ) || ( strlen( ++pos ) == 0 ) ) continue;
            if( strstr( line, "musicdir=" ) == line ) {
            	config->musicdir=falloc( strlen(pos)+1, sizeof( char ) );
            	strip( config->musicdir, pos, strlen(pos)+1 );
            }
            if( strstr( line, "channel=" ) == line ) {
            	config->channel=falloc( strlen(pos)+1, sizeof( char ) );
            	strip( config->channel, pos, strlen(pos)+1 );
            }
            if( strstr( line, "profiles=" ) == line ) {
            	config->profiles=scanparts( pos, &config->profile );
            }
            if( strstr( line, "streams=" ) == line ) {
            	config->streams=scanparts( pos, &config->stream );
            }
            if( strstr( line, "snames=" ) == line ) {
            	if( scanparts( pos, &config->sname ) != config->streams ) {
            		fail( F_FAIL, "Number of streams and stream names does not match!");
            	}
            }
            if( strstr( line, "active=" ) == line ) {
            	config->active=atoi(pos);
            }
            if( strstr( line, "skipdnp=" ) == line ) {
            	config->skipdnp=atoi(pos);
            }
            if( strstr( line, "fade=" ) == line ) {
            	config->fade=atoi(pos);
            }
        }
        while( !feof( fp ) );
    	return 0;
    }

   	return -1;
}

/**
 * writes the configuration from the given control structure into the file at
 * $HOME/.mixplay/mixplay.conf
 */
void writeConfig( mpconfig *config ) {
    char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    int		i;
    FILE    *fp;

    addMessage( 1, "Writing config\n" );

    snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );

	fp=fopen( conffile, "w" );

    if( NULL != fp ) {
        fprintf( fp, "[mixplay]" );
        fprintf( fp, "\nmusicdir=%s", config->musicdir );
        fprintf( fp, "\nprofiles=" );
        for( i=0; i< config->profiles; i++ ) {
        	fprintf( fp, "%s;", config->profile[i] );
        }
        fprintf( fp, "\nstreams=" );
        for( i=0; i< config->streams; i++ ) {
        	fprintf( fp, "%s;", config->stream[i] );
        }
        fprintf( fp, "\nsnames=" );
        for( i=0; i< config->streams; i++ ) {
        	fprintf( fp, "%s;", config->sname[i] );
        }
        fprintf( fp, "\nactive=%i", config->active );
        fprintf( fp, "\nskipdnp=%i", config->skipdnp );
        fprintf( fp, "\nfade=%i", config->fade );
        if( config->channel != NULL ) {
            fprintf( fp, "\nchannel=%s", config->channel );
        }
        else {
        	printf("\n# channel=Master  for standard installations");
        	printf("\n# channel=Digital for HifiBerry");
        	printf("\n# channel=Main");
        	printf("\n# channel=DAC");
        }
        fprintf( fp, "\n" );
        fclose( fp );
    }
    else {
        fail( errno, "Could not open %s", conffile );
    }
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
void freeConfig( mpconfig *control ) {
	int i;

	if( control == NULL ) {
		return;
	}

    sfree( control->dbname );
    sfree( control->dnpname );
    sfree( control->favname );
    sfree( control->musicdir );
    for( i=0; i<control->profiles; i++ ) {
    	sfree( control->profile[i] );
    }
    sfree( control->profile );

    for( i=0; i<control->streams; i++ ) {
    	sfree( control->stream[i] );
    	sfree( control->sname[i] );
    }
    sfree( control->stream );
    sfree( control->sname );

    control->root=cleanTitles( control->root );
}

/**
 * helperfunction to implement message ringbuffer
 */
static void msgbuffAdd( struct msgbuf_t msgbuf, char *line ) {
	msgbuf.msg[(msgbuf.current+msgbuf.lines)%10]=line;
	if( msgbuf.lines == 10 ) {
		msgbuf.current=(msgbuf.current+1)%10;
	}
	else {
		msgbuf.lines++;
	}
}

/**
 * helperfunction to implement message ringbuffer
 */
static char *msgbuffGet( struct msgbuf_t msgbuf ) {
	char *retval = NULL;
	if( msgbuf.lines > 0 ) {
		retval=msgbuf.msg[msgbuf.current];
		msgbuf.current = (msgbuf.current+1)%10;
		msgbuf.lines--;
	}
	return retval;
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

    assert( c_config != NULL );

    if( v >= getVerbosity() ) {
		pthread_mutex_lock( &msglock );
		line = falloc( MP_MSGLEN, sizeof(char) );
		va_start( args, msg );
		vsnprintf( line, MP_MSGLEN, msg, args );
		va_end( args );
    	if( ( c_config !=NULL ) && ( c_config->inUI ) ) {
			msgbuffAdd( c_config->msg, line );
    	}
    	else {
    		printf( "V%i %s\n", v, line );
    	}
		pthread_mutex_unlock( &msglock );
    }
}

/**
 * gets the current message removes it from the ring and frees the message buffer
 * returns 0 and makes msg an empty string if no message is in the ring
 */
int getMessage( char *msg ) {
	char *buf;

	assert( c_config != NULL );

	pthread_mutex_lock( &msglock );
	buf=msgbuffGet( c_config->msg );
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
