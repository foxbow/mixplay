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

void setSCommand( mpcmd cmd ) {
    pthread_mutex_lock( &cmdlock );
    getConfig()->command=cmd;
}


static size_t appendstr( char *start, const char *text ) {
	int pos;
	strcat( start, text );
	pos=strlen(text)+1;
	start[pos]=0;
	return pos;
}

static size_t getstring( const char *pos, char *buff ) {
	strcpy( buff, pos );
	return( strlen(buff)+1 );
}

static size_t getint( const char* pos, int *val ) {
	size_t len=0;
	for( len=0; len < sizeof( int ); len++ ) {
		((char *)val)[len]=*(pos+len);
		len++;
	}
	return len;

}

static size_t appendint( char *start, const int val ) {
	size_t len;
	for( len=0; len < sizeof( int ); len++ ) {
		*(start+len)=((char *)&val)[len];
		len++;
	}
	return len;
}

static size_t appendTitle( char *buff, const struct entry_t *title ) {
	size_t len=0;
	if( title != NULL ) {
		len+=appendstr( buff+len, title->artist );
		len+=appendstr( buff+len, title->album );
		len+=appendstr( buff+len, title->title );
		len+=appendint( buff+len, title->flags );
	}
	else {
		len+=appendstr( buff+len, "---" );
		len+=appendstr( buff+len, "---" );
		len+=appendstr( buff+len, "---" );
		len+=appendint( buff+len, 0 );
	}
	return len;
}

static size_t getTitle( const char *buff, struct entry_t *title ) {
	size_t len=0;
	len+=getstring( buff+len, title->artist );
	len+=getstring( buff+len, title->album );
	len+=getstring( buff+len, title->title );
	len+=getint( buff+len, (int*)&title->flags );
	sprintf( title->display, "%s - %s", title->artist, title->title );
	strcpy( title->path, "[mixplayd]" );
	return len;
}

size_t serialize( const mpconfig *data, char *buff ) {
	size_t len=0;

	memset( buff, 0, MP_MAXCOMLEN );

	if( data->current != NULL ) {
		len+=appendTitle( buff+len, data->current->plprev );
		len+=appendTitle( buff+len, data->current );
		len+=appendTitle( buff+len, data->current->plnext );
	}
	else {
		len+=appendTitle( buff+len, NULL );
		len+=appendTitle( buff+len, NULL );
		len+=appendTitle( buff+len, NULL );
	}
	len+=appendstr( buff+len, data->playtime );
	len+=appendstr( buff+len, data->remtime );
	len+=appendint( buff+len, data->percent );
	len+=appendint( buff+len, data->volume );
	len+=appendint( buff+len, data->status );
	len+=appendint( buff+len, data->playstream );
	
	return len;
}

size_t deserialize( mpconfig *data, const char *buff ) {
	size_t pos=0;

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
	pos+=getstring( buff+pos, data->playtime );
	pos+=getstring( buff+pos, data->remtime );
	pos+=getint( buff+pos, &data->percent );
	pos+=getint( buff+pos, &data->volume );
	pos+=getint( buff+pos, &data->status );
	pos+=getint( buff+pos, &data->playstream );
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
       fail( errno, "connect() failed!");
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
				addMessage( 1, "Server disconnected");
				config->status=mpc_quit;
				break;
			default:
				deserialize(config, commdata);
				updateUI( config );
			}
		}

        pthread_mutex_trylock( &cmdlock );
        if( config->command != mpc_idle ) {
        	send( sock , mpcString( config->command ), strlen( mpcString( config->command ) )+1, 0 );
        	config->command=mpc_idle;
        }
        pthread_mutex_unlock( &cmdlock );
    }

    addMessage( 1, "Disconnected");

    free( commdata );
    close(sock);
    config->rtid=0;
	return NULL;
}
