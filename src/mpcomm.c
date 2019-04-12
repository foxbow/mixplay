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

#ifndef MPCOMM_VER
#define MPCOMM_VER -1
#endif

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
	return client;
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
 * helperfunction to add a title to the given jsonOblect
 * if title is NULL an empty title will be created
 */
static jsonObject *jsonAddTitle( jsonObject *jo, const char *key, const mpplaylist *pl ) {
	jsonObject *val=NULL;
	mptitle *title=NULL;

	if( pl != NULL ) {
		title=pl->title;
	}

	if( title != NULL ) {
		val=jsonAddInt( NULL, "key", title->key );
		jsonAddStr( val, "artist", title->artist );
		jsonAddStr( val, "album", title->album );
		jsonAddStr( val, "title", title->title );
		jsonAddInt( val, "flags", title->flags );
		jsonAddStr( val, "genre", title->genre );
		jsonAddInt( val, "playcount", title->playcount );
		jsonAddInt( val, "skipcount", title->skipcount );
	}
	else {
		val=jsonAddInt( NULL, "key", 0 );
		jsonAddStr( val, "artist", "-" );
		jsonAddStr( val, "album", "-" );
		jsonAddStr( val, "title", getCurrentActivity() );
		jsonAddInt( val, "flags", 0 );
		jsonAddStr( val, "genre", "-" );
		jsonAddInt( val, "playcount", 0 );
		jsonAddInt( val, "skipcount", 0 );
	}

	return jsonAddObj(jo, key, val);
}

/**
 * turns a playlist into a jsonObject.
 */
static jsonObject *jsonAddTitles( jsonObject *jo, const char *key, mpplaylist *pl, int dir ) {
	jsonObject *jsonTitle=NULL;
	jsonObject *ret=NULL;
	char ikey[20];
	int i=0;

	while( pl != NULL ) {
		sprintf( ikey, "%i", i );
		jsonTitle=jsonAddTitle( jsonTitle, ikey, pl );
		if( i == 0 ) {
			ret=jsonTitle;
		}
		if( dir < 0 ) {
			pl=pl->prev;
		}
		else {
			pl=pl->next;
		}
		i++;
	}

	jo=jsonAddArr( jo, key, ret );

	return jo;
}

/**
 * put data to be sent over into the buff
 * adds messages only if any are available for the client
**/
char *serializeStatus( unsigned long *count, int clientid, int type ) {
	mpconfig *data=getConfig();
	jsonObject *jo=NULL;
	mpplaylist *current=data->current;

	jo=jsonAddInt( jo, "version", MPCOMM_VER );
	jsonAddInt( jo, "type", type );

	if( type == MPCOMM_FULLSTAT ) {
		if( current != NULL ) {
			jsonAddTitles( jo, "prev", current->prev, -1 );
			jsonAddTitle( jo, "current", current );
			jsonAddTitles( jo, "next", current->next, 1 );
		}
		else {
			jsonAddTitles( jo, "prev", NULL, -1 );
			jsonAddTitle( jo, "current", NULL );
			jsonAddTitles( jo, "next", NULL, 1 );
		}
		jsonAddStrs( jo, "playlist", data->playlist, data->playlists );
	}
	else if ( type == MPCOMM_RESULT ) {
		jsonAddTitles(jo, "titles", data->found->titles, 1 );
		jsonAddStrs(jo, "artists", data->found->artists, data->found->anum);
		jsonAddStrs(jo, "albums", data->found->albums, data->found->lnum);
		jsonAddStrs(jo, "albart", data->found->albart, data->found->lnum);
	}
	jsonAddInt( jo, "playstream", data->playstream );
	jsonAddInt( jo, "active", data->active );
	jsonAddStr( jo, "playtime", data->playtime );
	jsonAddStr( jo, "remtime", data->remtime );
	jsonAddInt( jo, "percent", data->percent );
	jsonAddInt( jo, "volume", data->volume );
	jsonAddInt( jo, "status", data->status );
	jsonAddBool( jo, "pledit", data->pledit );

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
