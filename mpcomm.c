/*
 * comm.c
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#include "mpcomm.h"
#include "utils.h"
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


/*
 * helperfunctions for de/serializing config and info
 */
static size_t appStr( char *start, const char *text ) {
	int pos;
	strcat( start, text );
	pos=strlen(text)+1;
	start[pos]=0;
	return pos;
}

static size_t getStr( const char *pos, char *buff ) {
	strcpy( buff, pos );
	return( strlen(buff)+1 );
}

static size_t getInt( const char* pos, int *val ) {
	size_t len=0;
	for( len=0; len < sizeof( int ); len++ ) {
		((char *)val)[len]=*(pos+len);
		len++;
	}
	return len;

}

static size_t appInt( char *start, const int val ) {
	size_t len;
	for( len=0; len < sizeof( int ); len++ ) {
		*(start+len)=((char *)&val)[len];
		len++;
	}
	return len;
}

static size_t appTitle( char *buff, const struct entry_t *title ) {
	size_t len=0;
	if( title != NULL ) {
		len+=appStr( buff+len, title->artist );
		len+=appStr( buff+len, title->album );
		len+=appStr( buff+len, title->title );
		len+=appInt( buff+len, title->flags );
	}
	else {
		len+=appStr( buff+len, "---" );
		len+=appStr( buff+len, "---" );
		len+=appStr( buff+len, "---" );
		len+=appInt( buff+len, 0 );
	}
	return len;
}

static size_t getTitle( const char *buff, struct entry_t *title ) {
	size_t len=0;
	len+=getStr( buff+len, title->artist );
	len+=getStr( buff+len, title->album );
	len+=getStr( buff+len, title->title );
	len+=getInt( buff+len, (int*)&title->flags );
	sprintf( title->display, "%s - %s", title->artist, title->title );
	strcpy( title->path, "[mixplayd]" );
	return len;
}

/*
 * put data to be sent over into the buff
 */
size_t serialize( const mpconfig *data, char *buff, long *count ) {
	size_t len=0;

	memset( buff, 0, MP_MAXCOMLEN );

	len+=appInt( buff+len,  MP_COMVER );
	if( data->current != NULL ) {
		len+=appTitle( buff+len, data->current->plprev );
		len+=appTitle( buff+len, data->current );
		len+=appTitle( buff+len, data->current->plnext );
	}
	else {
		len+=appTitle( buff+len, NULL );
		len+=appTitle( buff+len, NULL );
		len+=appTitle( buff+len, NULL );
	}
	len+=appStr( buff+len, data->playtime );
	len+=appStr( buff+len, data->remtime );
	len+=appInt( buff+len, data->percent );
	len+=appInt( buff+len, data->volume );
	len+=appInt( buff+len, data->status );
	len+=appInt( buff+len, data->playstream );
	if( *count < data->msg->count ) {
		len+=appStr( buff+len, msgBuffPeek( data->msg ) );
		(*count)++;
	}
	else {
		len+=appStr( buff+len, "" );
	}
	return len;
}

/*
 * sort data from buff into the current configuration
 */
size_t deserialize( mpconfig *data, const char *buff ) {
	size_t pos=0;
	int ver;
	char msgline[128];

	pos+=getInt( buff+pos, &ver );
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
	pos+=getTitle( buff+pos, data->current->plprev );
	pos+=getTitle( buff+pos, data->current );
	pos+=getTitle( buff+pos, data->current->plnext );
	pos+=getStr( buff+pos, data->playtime );
	pos+=getStr( buff+pos, data->remtime );
	pos+=getInt( buff+pos, &data->percent );
	pos+=getInt( buff+pos, &data->volume );
	pos+=getInt( buff+pos, &data->status );
	pos+=getInt( buff+pos, &data->playstream );
	pos+=getStr( buff+pos, msgline );
	if( strlen( msgline ) > 0  ){
		addMessage( 0, msgline );
	}
	return pos;
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
    	to.tv_usec=100000; /* 1/2 second */
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
				if( deserialize(config, commdata) == -1 ) {
					goto cleanup;
				}
				else {
					updateUI( config );
				}
			}
		}

        pthread_mutex_trylock( &cmdlock );
        if( config->command != mpc_idle ) {
        	send( sock , mpcString( config->command ), strlen( mpcString( config->command ) )+1, 0 );
        	config->command=mpc_idle;
        }
        pthread_mutex_unlock( &cmdlock );
    }

cleanup:
    addMessage( 1, "Disconnected");

    free( commdata );
    close(sock);
    config->rtid=0;
	return NULL;
}
