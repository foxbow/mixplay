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
#include <string.h>
#include <stdio.h>
#include "errno.h"
#include "config.h"
#include "utils.h"
#include "player.h"
#include "musicmgr.h"

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
			strip( (*target)[i], line, MAXPATHLEN );
			line=pos+1;
		}
	}

	return num;
}

/**
 * reads the configuration file at $HOME/.mixplay/mixplay.conf and stores the settings
 * in the given control structure.
 */
int readConfig( struct mpcontrol_t *config ) {
    char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    char	line[MAXPATHLEN];
    char	*pos;
    FILE    *fp;

    printver( 1, "Writing config\n" );

    snprintf( conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", getenv( "HOME" ) );

	fp=fopen( conffile, "r" );

    if( NULL != fp ) {
        do {
            fgets( line, MAXPATHLEN, fp );
            pos=strchr( line, '=' )+1;
            if( strstr( line, "musicdir=" ) == line ) {
            	config->musicdir=falloc( strlen(pos)+1, sizeof( char ) );
            	strip( config->musicdir, pos, MAXPATHLEN );
            }
            if( strstr( line, "profiles=" ) == line ) {
            	config->profiles=scanparts( line, &config->profile );
            }
            if( strstr( line, "streams=" ) == line ) {
            	config->streams=scanparts( line, &config->stream );
            }
            if( strstr( line, "snames=" ) == line ) {
            	if( scanparts( line, &config->stream ) != config->streams ) {
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
void writeConfig( struct mpcontrol_t *config ) {
    char	conffile[MAXPATHLEN]; /*  = "mixplay.conf"; */
    int		i;
    FILE    *fp;

    printver( 1, "Writing config\n" );

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
        fprintf( fp, "\n" );
        fclose( fp );
    }
    else {
        fail( errno, "Could not open %s", conffile );
    }
}

