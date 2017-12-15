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

static pthread_mutex_t cmdlock=PTHREAD_MUTEX_INITIALIZER;

/*
 * sets the command to be sent to mixplayd
 */
void setSCommand( mpcmd cmd ) {
    pthread_mutex_lock( &cmdlock );
    getConfig()->command=cmd;
}

static jsonObject *appTitle( jsonObject *jo, const char *key, const struct entry_t *title ) {
	jsonObject *val=NULL;

	if( title != NULL ) {
		val=jsonAddStr( val, "artist", title->artist );
		jsonAddStr( val, "album", title->album );
		jsonAddStr( val, "title", title->title );
		jsonAddInt( val, "flags", title->flags );
	}
	else {
		val=jsonAddStr( val, "artist", "---" );
		jsonAddStr( val, ",album", "---" );
		jsonAddStr( val, ",title", "---" );
		jsonAddInt( val, ",flags", 0 );
	}
	return jsonAddObj(jo, key, val);
}

static void getTitle( jsonObject *jo, const char *key, struct entry_t *title ) {
	jsonObject *to;
	to=jsonGetObj( jo, key);

	if( to != NULL ) {
		jsonCopyStr(to, "artist", title->artist);
		jsonCopyStr(to, "album", title->album);
		jsonCopyStr(to, "title", title->title);
		title->flags=jsonGetInt( to, "flags" );
		sprintf( title->display, "%s - %s", title->artist, title->title );
	}
	else {
		strcpy( title->display, "No title found.." );
	}
	strcpy( title->path, "[mixplayd]" );
}

/**
 * put data to be sent over into the buff
**/
size_t serialize( const mpconfig *data, char *buff, long *count ) {
	jsonObject *joroot=NULL;
	jsonObject *jo=NULL;

	buff[0]=0;

	jo=jsonAddInt( jo, "version", MP_COMVER );
	joroot=jo;
	if( data->current != NULL ) {
		jo=appTitle( jo, "prev", data->current->plprev );
		jo=appTitle( jo, "current", data->current );
		jo=appTitle( jo, "next", data->current->plnext );
	}
	else {
		jo=appTitle( jo, "prev", NULL );
		jo=appTitle( jo, "current", NULL );
		jo=appTitle( jo, "next", NULL );
	}
	jo=jsonAddStr( jo, "playtime", data->playtime );
	jo=jsonAddStr( jo, "remtime", data->remtime );
	jo=jsonAddInt( jo, "percent", data->percent );
	jo=jsonAddInt( jo, "volume", data->volume );
	jo=jsonAddInt( jo, "status", data->status );
	jo=jsonAddInt( jo, "playstream", data->playstream );
	if( *count < data->msg->count ) {
		jo=jsonAddStr( jo, "msg", msgBuffPeek( data->msg ) );
		(*count)++;
	}
	else {
		jo=jsonAddStr( jo, "msg", "" );
	}

	jsonWrite(joroot, buff);
	jsonDiscard( joroot, 1 );

	return strlen(buff);
}

/*
 * sort data from buff into the current configuration
 */
size_t deserialize( mpconfig *data, char *json ) {
	int ver;
	char msgline[128];
	jsonObject *jo;

	jo=jsonParse( json );
	if( jo != NULL ) {
		ver=jsonGetInt(jo, "version" );
		if( ver != MP_COMVER ) {
			fail( F_FAIL, "mixplayd protocol mismatch!\nServer has version %i, we have version %i!", ver, MP_COMVER );
			return -1;
		}
		if( data->current == NULL ) {
			/* dummy path to fill artist, album, title */
			data->current=insertTitle( data->current, "server/mixplayd/title" );
			data->root=data->current;
			addToPL( data->current, data->current );
			addToPL( insertTitle( data->current, "server/mixplayd/title" ), data->current );
			addToPL( insertTitle( data->current, "server/mixplayd/title" ), data->current );
		}

		getTitle( jo, "prev", data->current->plprev );
		getTitle( jo, "current", data->current );
		getTitle( jo, "next", data->current->plnext );
		jsonCopyStr( jo, "playtime", data->playtime );
		jsonCopyStr( jo, "remtime", data->remtime );
		data->percent=jsonGetInt( jo, "percent" );
		data->volume=jsonGetInt( jo, "volume" );
		data->status=jsonGetInt( jo, "status" );
		data->playstream=jsonGetInt( jo, "playstream" );
		jsonCopyStr( jo, "msg", msgline );
		if( strlen( msgline ) > 0  ){
			addMessage( 0, msgline );
		}
		jsonDiscard( jo, 0 );
	}
	else {
		addMessage( 1, "Recieved no data.." );
	}

	return 0;
}

static int recFromMixplayd( mpconfig *config, char *data ) {
	char *pos=data;
	int empty=1;
	while( pos[1] != 0 ) {
		if( ( pos[0] == 0x0d ) && ( pos[1] == 0x0a ) ) {
			if( empty ) {
				break;
			}
			else {
				empty=1;
				pos++;
			}
		}
		else {
			empty=0;
		}
		pos++;
	}
	if( pos[1] == 0 ) return -1;
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
    int state=0;

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

    addMessage( 1, "Connected");

    while( config->status != mpc_quit ) {
    	FD_ZERO( &fds );
    	FD_SET( sock, &fds );

    	to.tv_sec=0;
    	to.tv_usec=500000; /* 1/2 second */
    	select( FD_SETSIZE, &fds, NULL, NULL, &to );

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
				if( recFromMixplayd(config, commdata) == -1 ) {
					goto cleanup;
				}
				else {
					state = 2;
					updateUI( config );
				}
			}
		}

        pthread_mutex_trylock( &cmdlock );
        len=0;
        if( config->command != mpc_idle ) {
        	sprintf( commdata, "get /cmd/%s HTTP/1.1\015\012xmixplay: 1\015\012\015\012", mpcString( config->command ) );
        	while( len < strlen( commdata ) ) {
        		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
        	}
            config->command=mpc_idle;
        }
        else if( config->command == mpc_quit ) {
        	config->status=mpc_quit;
        }
        else if( state == 0 ){
           	sprintf( commdata, "get /status HTTP/1.1\015\012xmixplay: 1\015\012\015\012" );
           	while( len < strlen( commdata ) ) {
           		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
           	}
           	state=1;
        }

        if( state== 2){
        	state=0;
        }
        pthread_mutex_unlock( &cmdlock );
    }

cleanup:
    addMessage( 1, "Disconnected");
    config->status=mpc_quit;
    free( commdata );
    close(sock);
    config->rtid=0;
	return NULL;
}
