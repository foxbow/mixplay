/*
 * mpcomm.c
 *
 *  Created on: 29.11.2017
 *	  Author: bweber
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

static pthread_mutex_t _scmdlock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _clientlock=PTHREAD_MUTEX_INITIALIZER;
static int _curclient=-1;

/*
 * all of the next messages will only be sent to this client
 * other requests to set an exclusive client will be blocked
 * until the current client is unlocked
 *
 * Even if it may look tempting, queuing the calls is not
 * worth the effort!
 */
int setCurClient( int client ) {
	if( pthread_mutex_trylock( &_clientlock ) == EBUSY ) {
		return -1;
	}
	_curclient=client;
	return 0;
}

int isCurClient( int client ) {
	return( _curclient == client );
}

/*
 * sends all next messages to every client and allows other clients
 * to lock the messages
 * only unlock if we really are the current client. Otherwise this is just a clean-up
 * call to avoid a deadlock.
 */
void unlockClient( int client ) {
	if( client == _curclient ) {
		_curclient=-1;
		pthread_mutex_unlock( &_clientlock );
		return;
	}
}

/*
 * sets the command to be sent to mixplayd
 */
void setSCommand( mpcmd cmd ) {
	pthread_mutex_lock( &_scmdlock );
	getConfig()->command=cmd;
}

/*
 * helperfunction to add a title to the given jsonOblect
 * if title is NULL an empty title will be created
 */
static jsonObject *jsonAddTitle( jsonObject *jo, const char *key, const mptitle *title ) {
	jsonObject *val=NULL;

	if( title != NULL ) {
		val=jsonAddStr( NULL, "artist", title->artist );
		jsonAddStr( val, "album", title->album );
		jsonAddStr( val, "title", title->title );
		jsonAddInt( val, "flags", title->flags );
		jsonAddStr( val, "genre", title->genre );
		jsonAddInt( val, "playcount", title->playcount );
		jsonAddInt( val, "skipcount", title->skipcount );
	}
	else {
		val=jsonAddStr( NULL, "artist", "-" );
		jsonAddStr( val, ",album", "-" );
		jsonAddStr( val, "title", "-" );
		jsonAddInt( val, "flags", 0 );
		jsonAddStr( val, "genre", "-" );
		jsonAddInt( val, "playcount", 0 );
		jsonAddInt( val, "skipcount", 0 );
	}
	return jsonAddObj(jo, key, val);
}

/*
 * helperfunction to retrieve title data from the given jsonOblect
 */
static void jsonGetTitle( jsonObject *jo, const char *key, mptitle *title ) {
	jsonObject *to;
	to=jsonGetObj( jo, key);

	if( to != NULL ) {
		jsonCopyStr(to, "artist", title->artist, NAMELEN );
		jsonCopyStr(to, "album", title->album, NAMELEN );
		jsonCopyStr(to, "title", title->title, NAMELEN );
		title->flags=jsonGetInt( to, "flags" );
		jsonCopyStr(to, "genre", title->genre, NAMELEN );
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
 * turns a playlist into a jsonObject.
 */
static jsonObject *jsonAddTitles( jsonObject *jo, const char *key, mpplaylist *pl ) {
	jsonObject *title=NULL;
	char ikey[20];
	int index=0;

	while( pl != NULL ) {
		sprintf( ikey, "%i", index );
		title=jsonAddTitle( title, ikey, pl->title );
		pl=pl->next;
		index++;
	}
	jo=jsonAddArr( jo, key, title );

	return jo;
}

/**
 * appends titles to the given playlist
 * caveat: pl must not be NULL!
 * todo: enable fetching into an empty list
 */
int jsonGetTitles( jsonObject *jo, const char *key, mpplaylist *pl ) {
	mpplaylist *buf=NULL;
	char ikey[20]="0";
	int index=0;

	while( jsonPeek( jo, ikey ) != json_null ) {
		if( pl->next == NULL ) {
			pl->next=falloc(1, sizeof(mpplaylist) );
			pl->next->next=NULL;
			pl->next->prev=pl;
			pl->title=falloc(1,sizeof(mptitle) );
		}
		pl=pl->next;
		jsonGetTitle( jo, ikey, pl->title );
		index++;
	}

	/* remove playlist elements that were not updated */
	pl=pl->next;
	while( pl != NULL ) {
		if( pl->prev != NULL ) {
			pl->prev->next=NULL;
		}
		buf=pl->next;
		if( buf != NULL ) {
			buf->prev=NULL;
		}
		free(pl->title);
		free(pl);
		pl=buf;
	}

	return index;
}

/**
 * put data to be sent over into the buff
 * adds messages only if any are available for the client
**/
char *serializeStatus( unsigned long *count, int clientid, int fullstat ) {
	mpconfig *data=getConfig();
	jsonObject *jo=NULL;
	mpplaylist *current=data->current;

	jo=jsonAddInt( jo, "version", MPCOMM_VER );

	if( fullstat ) {
		jsonAddInt( jo, "type", MPCOMM_FULLSTAT );
		if( current != NULL ) {
			if( current->prev != NULL ) {
				jsonAddTitle( jo, "prev", current->prev->title );
			}
			else {
				jsonAddTitle( jo, "prev", NULL );
			}
			jsonAddTitle( jo, "current", data->current->title );
			jsonAddTitles( jo, "next", current->next );
		}
		else {
			jsonAddTitle( jo, "prev", NULL );
			jsonAddTitle( jo, "current", NULL );
			jsonAddTitles( jo, "next", NULL );
		}
	}
	else {
		jsonAddInt( jo, "type", MPCOMM_STAT );
	}
	jsonAddInt( jo, "playstream", data->playstream );
	jsonAddInt( jo, "active", data->active );
	jsonAddStr( jo, "playtime", data->playtime );
	jsonAddStr( jo, "remtime", data->remtime );
	jsonAddInt( jo, "percent", data->percent );
	jsonAddInt( jo, "volume", data->volume );
	jsonAddInt( jo, "status", data->status );

	/* broadcast */
	if( _curclient == -1 ) {
		if( *count < data->msg->count ) {
			jsonAddStr( jo, "msg", msgBuffPeek( data->msg, *count ) );
			*count=(*count)+1;
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

	return jsonToString( jo );
}

/**
 * global/static part of the given config
 */
char *serializeConfig( void ) {
	mpconfig *config=getConfig();
	jsonObject *joroot=NULL;
	jsonObject *jo=NULL;

	/* start with version */
	joroot=jsonAddInt( NULL, "version", MPCOMM_VER );
	jsonAddInt( joroot, "type", MPCOMM_CONFIG );

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

	return jsonToString( joroot );
}

/*
 * store the global/static configuration from the given jsonObject in the
 * given config
 */
static int deserializeConfig( jsonObject *jo ) {
	mpconfig *config=getConfig();
	jsonObject *joconf=NULL;
	joconf=jsonGetObj( jo, "config" );
	if( joconf == NULL ) {
		addMessage( 0, "No config in reply!" );
		return -1;
	}

	freeConfigContents( );

	config->fade=jsonGetInt( joconf, "fade");
	config->musicdir=jsonGetStr( joconf, "musicdir" );
	config->profiles=jsonGetInt( joconf, "profiles" );
	config->profile=jsonGetStrs( joconf, "profile", config->profiles );
	config->skipdnp=jsonGetInt( joconf, "skipdnp" );
	config->streams=jsonGetInt( joconf, "streams" );
	config->stream=jsonGetStrs( joconf, "stream", config->streams );
	config->sname=jsonGetStrs( joconf, "sname", config->streams );

	return 0;
}

/**
 * reads the status into the global configuration
 */
static int deserializeStatus( jsonObject *jo ) {
	mpconfig *data=getConfig();
	char msgline[128];

	if( data->current == NULL ) {
		/* dummy path to fill artist, album, title */
		data->current=addPLDummy( NULL, "server/mixplayd/title" );
		addPLDummy( data->current, "server/mixplayd/title" );
		addPLDummy( data->current, "server/mixplayd/title" );
		addPLDummy( data->current, "server/mixplayd/title" );
	}
	if( jsonGetInt(jo, "type" ) == MPCOMM_FULLSTAT ) {
		jsonGetTitle( jo, "prev", data->current->prev->title );
		jsonGetTitle( jo, "current", data->current->title );
		jsonGetTitles( jo, "next", data->current );
	}
	data->active=jsonGetInt( jo, "active");
	data->playstream=jsonGetInt( jo, "playstream" );
	jsonCopyStr( jo, "playtime", data->playtime, 10 );
	jsonCopyStr( jo, "remtime", data->remtime, 10 );
	data->percent=jsonGetInt( jo, "percent" );
	data->volume=jsonGetInt( jo, "volume" );
	data->status=jsonGetInt( jo, "status" );
	jsonCopyStr( jo, "msg", msgline, 128 );
	if( strlen( msgline ) > 0 ){
		addMessage( 0, msgline );
	}

	return 0;
}
/*
 * sort data from buff into the current configuration
 */
static int deserialize( char *json ) {
	int ver;
	jsonObject *jo;
	int retval=-1;

	jo=jsonRead( json );
	if( jo != NULL ) {
		ver=jsonGetInt(jo, "version" );
		if( ver < MPCOMM_VER ) {
			fail( F_FAIL, "mixplayd protocol mismatch!\nServer has version %i, we have version %i!", ver, MPCOMM_VER );
			return -1;
		}

		switch( jsonGetInt( jo, "type" ) ) {
		case MPCOMM_STAT:
		case MPCOMM_FULLSTAT: /* status */
			retval=deserializeStatus( jo );
			break;

		case MPCOMM_CONFIG: /* config */
			retval=deserializeConfig( jo );
			break;

		default:
			addMessage( 1, "Unknown reply type!" );
		}
	}
	jsonDiscard( jo );

	return retval;
}

static int recFromMixplayd( char *data ) {
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
	return deserialize( &pos[2] );
}

/**
 * analog to the reader() in player.c
 */
void *netreader( void *control ) {
	int sock, intr;
	struct sockaddr_in server;
	char *commdata;
	size_t commsize=MP_BLKSIZE;
	mpconfig *config;
	struct timeval to;
	fd_set fds;
	size_t len=0;
	ssize_t recvd, retval;

	commdata=falloc( commsize, sizeof( char ) );
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

	sprintf( commdata, "get /config HTTP/1.1\015\012xmixplay: 1\015\012\015\012" );
	while( len < strlen( commdata ) ) {
		len+=send( sock , &commdata[len], strlen( commdata )-len, 0 );
	}

	while( config->status != mpc_quit ) {
		len=0;
		FD_ZERO( &fds );
		FD_SET( sock, &fds );
		to.tv_sec=0;
		to.tv_usec=250000; /* 1/4 second */
		intr=select( FD_SETSIZE, &fds, NULL, NULL, &to );

		/* is data available */
		if( FD_ISSET( sock, &fds ) ) {
			memset( commdata, 0, commsize );
			recvd=0;
			while( ( retval=recv( sock, commdata+recvd, commsize-recvd, 0 ) ) == commsize-recvd ) {
				recvd=commsize;
				commsize+=MP_BLKSIZE;
				commdata=frealloc( commdata, commsize );
				memset( commdata+recvd, 0, MP_BLKSIZE );
			}
			switch( retval ) {
			case -1:
				addMessage( 0, "Read error on socket!\n%s", strerror( errno ) );
				config->status=mpc_quit;
				break;
			case 0:
				addMessage( 0, "mixplayd has disconnected the session");
				config->status=mpc_quit;
				break;
			default:
				if( recFromMixplayd( commdata ) == 0 ) {
					updateUI( );
				}
			}
		}

		/* reset buffer */
		commdata[0]=0;

		/* check current command */
		pthread_mutex_trylock( &_scmdlock );
		switch( MPC_CMD(config->command) ) {
		case mpc_quit:
			config->status=mpc_quit;
			break;
		/* commands that send an argument */
		case mpc_profile:
		case mpc_newprof:
		case mpc_search:
		case mpc_setvol:
		case mpc_fav:
		case mpc_dnp:
			if( config->argument != NULL ) {
				sprintf( commdata, "get /cmd/%04x?%s HTTP/1.1\015\012xmixplay: 1\015\012\015\012",
						config->command, config->argument );
			}
			else {
				addMessage( 0, "No argument for %s!", mpcString( config->command ) );
			}
			break;
		case mpc_idle:
			/* nothing happened? Then ask for the current status
			 * otherwise an update reply would trigger yet another update and
			 * turn this into a CPU/network hog loop!
			 */
			if( intr == 0) {
				sprintf( commdata, "get /status HTTP/1.1\015\012xmixplay: 1\015\012\015\012" );
			}
			break;
		/* just forward any other command */
		default:
			sprintf( commdata, "get /cmd/%04x HTTP/1.1\015\012xmixplay: 1\015\012\015\012", config->command );
			break;
		}

		config->command=mpc_idle;
		sfree( &(config->argument) );

		while( len < strlen( commdata ) ) {
			retval=send( sock , commdata+len, strlen( commdata )-len, 0 );
			switch( retval ) {
			case -1:
				addMessage( 0, "Write error on socket!\n%s", strerror( errno ) );
				config->status=mpc_quit;
				len=strlen( commdata );
				break;
			case 0:
				addMessage( 0, "mixplayd has disconnected the session");
				config->status=mpc_quit;
				len=strlen( commdata );
				break;
			default:
				len+=retval;
			}
		}

		pthread_mutex_unlock( &_scmdlock );
	}

	addMessage( 1, "Disconnected");
	updateUI( );
	free( commdata );
	close(sock);
	config->rtid=0;
	return NULL;
}
