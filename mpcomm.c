/*
 * comm.c
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#include "mpcomm.h"
#include "utils.h"
#include "json.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

static pthread_mutex_t _cmdlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _clientlock=PTHREAD_MUTEX_INITIALIZER;
static int _curclient=-1;

void setCurClient( int client ) {
	pthread_mutex_lock( &_clientlock );
	_curclient=client;
}

void unlockClient( int client ) {
	if( client == _curclient ) {
		_curclient=-1;
		pthread_mutex_unlock( &_clientlock );
	}
}

/*
 * sets the command to be sent to mixplayd
 */
void setSCommand( mpcmd cmd ) {
    pthread_mutex_lock( &_cmdlock );
    getConfig()->command=cmd;
}

static jsonObject *appTitle( jsonObject *jo, const char *key, const struct entry_t *title ) {
	jsonObject *val=NULL;

	if( title != NULL ) {
		val=jsonAddStr( NULL, "artist", title->artist );
		jsonAddStr( val, "album", title->album );
		jsonAddStr( val, "title", title->title );
		jsonAddInt( val, "flags", title->flags );
		jsonAddInt( val, "playcount", title->playcount );
		jsonAddInt( val, "skipcount", title->skipcount );
	}
	else {
		val=jsonAddStr( NULL, "artist", "---" );
		jsonAddStr( val, ",album", "---" );
		jsonAddStr( val, ",title", "---" );
		jsonAddInt( val, ",flags", 0 );
		jsonAddInt( val, "playcount", 0 );
		jsonAddInt( val, "skipcount", 0 );
	}
	return jsonAddObj(jo, key, val);
}

static void getTitle( jsonObject *jo, const char *key, struct entry_t *title ) {
	jsonObject *to;
	to=jsonGetObj( jo, key);

	if( to != NULL ) {
		jsonCopyChars(to, "artist", title->artist);
		jsonCopyChars(to, "album", title->album);
		jsonCopyChars(to, "title", title->title);
		title->flags=jsonGetInt( to, "flags" );
		title->playcount=jsonGetInt( to, "playcount" );
		title->skipcount=jsonGetInt( to, "skipcount" );
		sprintf( title->display, "%s - %s", title->artist, title->title );
	}
	else {
		strcpy( title->display, "No title found.." );
	}
	strcpy( title->path, "[mixplayd]" );
}

/**
 * put data to be sent over into the buff
 * adds messages only if any are available for the client
**/
size_t serializeStatus( const mpconfig *data, char *buff, long *count, int clientid ) {
	jsonObject *jo=NULL;

	buff[0]=0;

	jo=jsonAddInt( NULL, "version", MP_COMVER );
	jsonAddInt( jo, "type", 1 );

	if( data->current != NULL ) {
		appTitle( jo, "prev", data->current->plprev );
		appTitle( jo, "current", data->current );
		appTitle( jo, "next", data->current->plnext );
	}
	else {
		appTitle( jo, "prev", NULL );
		appTitle( jo, "current", NULL );
		appTitle( jo, "next", NULL );
	}
	jsonAddStr( jo, "playtime", data->playtime );
	jsonAddStr( jo, "remtime", data->remtime );
	jsonAddInt( jo, "percent", data->percent );
	jsonAddInt( jo, "volume", data->volume );
	jsonAddInt( jo, "active", data->active );
	jsonAddInt( jo, "status", data->status );
	jsonAddInt( jo, "playstream", data->playstream );

	/* broadcast */
	if( _curclient == -1 ) {
		if( *count < data->msg->count ) {
			jsonAddStr( jo, "msg", msgBuffPeek( data->msg, *count ) );
			(*count)++;
		}
		else {
			jsonAddStr( jo, "msg", "" );
		}
	}
	/* direct send */
	else if( clientid == _curclient ) {
		if( *count < data->msg->count ) {
			if( strcmp( "Done.", msgBuffPeek( data->msg, *count ) ) == 0 ) {
				unlockClient( clientid );
			}
			jsonAddStr( jo, "msg", msgBuffPeek( data->msg, *count ) );
			(*count)++;
		}
		else {
			jsonAddStr( jo, "msg", "" );
		}
	}
	/* not for the current client */
	else {
		(*count)=getConfig()->msg->count;
		jsonAddStr( jo, "msg", "" );
	}

	jsonWrite(jo, buff);
	jsonDiscard( jo, -1 );

	return strlen(buff);
}

/**
 * global/static part of the config
 */
size_t serializeConfig( mpconfig *config, char *buff ) {
	jsonObject *joroot=NULL;
	jsonObject *jo=NULL;

	buff[0]=0;

	/* start with version */
	joroot=jsonAddInt( NULL, "version", MP_COMVER );
	jsonAddInt( joroot, "type", 2 );

	/* dump config into JSON object */
	jo=jsonAddInt( NULL, "fade", config->fade );
	jsonAddStr( jo, "musicdir", config->musicdir );
	jsonAddInt( jo, "profiles", config->profiles );
	jsonAddStrs( jo, "profile", config->profile, config->profiles );
	jsonAddInt( jo, "skipdnp", config->skipdnp );
	jsonAddInt( jo, "streams", config->streams );
	jsonAddStrs( jo, "stream", config->stream, config->streams );
	jsonAddStrs( jo, "sname", config->sname, config->streams );

	/* add config object to the root object */
	jsonAddObj( joroot, "config", jo );

	jsonWrite(joroot, buff);
	jsonDiscard( joroot, -1 );

	return strlen(buff);
}

static int deserializeConfig( mpconfig *config, jsonObject *jo ) {
	jsonObject *joconf=NULL;
	joconf=jsonGetObj( jo, "config" );
	if( joconf == NULL ) {
		addMessage( 0, "No config in reply!" );
		return -1;
	}

	freeConfigContents( config );

	config->fade=jsonGetInt( joconf, "fade");
	jsonCopyStr( joconf, "musicdir", &(config->musicdir) );
	config->profiles=jsonGetInt( joconf, "profiles" );
	jsonCopyStrs( joconf, "profile", &(config->profile), config->profiles );
	config->skipdnp=jsonGetInt( joconf, "skipdnp" );
	config->streams=jsonGetInt( joconf, "streams" );
	jsonCopyStrs( joconf, "stream", &(config->stream), config->streams );
	jsonCopyStrs( joconf, "sname", &(config->sname), config->streams );

	return 0;
}

static int deserializeStatus( mpconfig *data, jsonObject *jo ) {
	char msgline[128];

	if( data->current == NULL ) {
		/* dummy path to fill artist, album, title */
		data->current=insertTitle( data->current, "server/mixplayd/title" );
		data->root=data->current;
		addToPL( data->current, data->current );
		addToPL( insertTitle( data->current, "server/mixplayd/title" ), data->current );
		addToPL( insertTitle( data->current, "server/mixplayd/title" ), data->current );
	}
	data->active=jsonGetInt( jo, "active");
	getTitle( jo, "prev", data->current->plprev );
	getTitle( jo, "current", data->current );
	getTitle( jo, "next", data->current->plnext );
	jsonCopyChars( jo, "playtime", data->playtime );
	jsonCopyChars( jo, "remtime", data->remtime );
	data->percent=jsonGetInt( jo, "percent" );
	data->volume=jsonGetInt( jo, "volume" );
	data->status=jsonGetInt( jo, "status" );
	data->playstream=jsonGetInt( jo, "playstream" );
	jsonCopyChars( jo, "msg", msgline );
	if( strlen( msgline ) > 0 ){
		addMessage( 0, msgline );
	}

	return 0;
}
/*
 * sort data from buff into the current configuration
 */
static int deserialize( mpconfig *data, char *json ) {
	int ver;
	jsonObject *jo;
	int retval=-1;

	jo=jsonRead( json );
	if( jo != NULL ) {
		ver=jsonGetInt(jo, "version" );
		if( ver < MP_COMVER ) {
			fail( F_FAIL, "mixplayd protocol mismatch!\nServer has version %i, we have version %i!", ver, MP_COMVER );
			return -1;
		}

		switch( jsonGetInt( jo, "type" ) ) {
		case 1: /* status */
			retval=deserializeStatus( data, jo );
			break;

		case 2: /* config */
			retval=deserializeConfig( data, jo );
			break;

		default:
			addMessage( 1, "Unknown reply type!" );
		}
	}
	jsonDiscard( jo, 0 );

	return retval;
}

static int recFromMixplayd( mpconfig *config, char *data ) {
	char *pos=data;
	int empty=1;
	while( pos[1] != 0 ) {
		if( ( pos[0] == 0x0d ) && ( pos[1] == 0x0a ) ) {
			if( empty == 1 ) {
				break;
			}
			else if( pos[2] != 0 ){
				empty=1;
				pos++;
			}
		}
		else {
			empty=0;
		}
		pos++;
	}
	if( pos[1] == 0 ) {
		return -1;
	}
	return deserialize( config, &pos[2] );
}

/**
 * analog to the reader() in player.c
 */
void *netreader( void *control ) {
    int sock;
    struct sockaddr_in server;
    char *commdata;
    mpconfig *config;
    struct timeval to;
    fd_set fds;
    size_t len;
    int state=-1;

    commdata=falloc( MP_MAXCOMLEN, sizeof( char ) );
    config=(mpconfig*)control;

    sock = socket(AF_INET , SOCK_STREAM , 0);
    if (sock == -1) {
        fail( errno, "Could not create socket!");
    }

    server.sin_addr.s_addr = inet_addr( config->host );
    server.sin_family = AF_INET;
    server.sin_port = htons( config->port );

    if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0) {
       fail( errno, "Could not connect to %s on port %i\nIs mixplayd running?",
    		   config->host, config->port );
    }

    addMessage( 1, "Connected" );

    while( config->status != mpc_quit ) {
    	FD_ZERO( &fds );
    	FD_SET( sock, &fds );

    	to.tv_sec=0;
    	to.tv_usec=250000; /* 1/4 second */
    	select( FD_SETSIZE, &fds, NULL, NULL, &to );

		memset( commdata, 0, MP_MAXCOMLEN );

		if( FD_ISSET( sock, &fds ) ) {
			switch( recv(sock , commdata , MP_MAXCOMLEN , 0) ) {
			case -1:
				addMessage( 0, "Read error on socket!\n%s", strerror( errno ) );
				config->status=mpc_quit;
				break;
			case 0:
				addMessage( 0, "mixplayd has disconnected the session");
				config->status=mpc_quit;
				break;
			default:
				if( recFromMixplayd(config, commdata) == 0 ) {
					state = 2;
					updateUI( config );
				}
			}
		}

        pthread_mutex_trylock( &_cmdlock );
        len=0;
        if( config->command == mpc_quit ) {
        	config->status=mpc_quit;
        }
        else if( config->command != mpc_idle ) {
        	if( config->command == mpc_profile ) {
            	sprintf( commdata, "get /cmd/%s?%i HTTP/1.1\015\012xmixplay: 1\015\012\015\012",
            			mpcString( config->command ), config->active );
        	}
        	else if( config->command == mpc_search ) {
        		if( config->argument != NULL ) {
        			sprintf( commdata, "get /cmd/%s?%s HTTP/1.1\015\012xmixplay: 1\015\012\015\012",
        					mpcString( config->command ), config->argument );
        		}
        		else {
        			addMessage( 0, "No searchterm given!" );
        		}
        	}
        	else {
        		sprintf( commdata, "get /cmd/%s HTTP/1.1\015\012xmixplay: 1\015\012\015\012", mpcString( config->command ) );
        	}
        	while( len < strlen( commdata ) ) {
        		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
        	}
            config->command=mpc_idle;
        }
        else if( state == 0 ){
           	sprintf( commdata, "get /status HTTP/1.1\015\012xmixplay: 1\015\012\015\012" );
           	while( len < strlen( commdata ) ) {
           		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
           	}
           	state=1;
        }
        else if( state == -1 ) {
           	sprintf( commdata, "get /config HTTP/1.1\015\012xmixplay: 1\015\012\015\012" );
           	while( len < strlen( commdata ) ) {
           		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
           	}
           	state=0;
        }

        if( state== 2){
        	state=0;
        }
        pthread_mutex_unlock( &_cmdlock );
    }

    addMessage( 1, "Disconnected");
    updateUI(config);
    free( commdata );
    close(sock);
    config->rtid=0;
	return NULL;
}
