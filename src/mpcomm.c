/*
 * mpcomm.c
 *
 *  Created on: 29.11.2017
 *	  Author: bweber
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include "mpcomm.h"
#include "utils.h"

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
		if( _curclient == client ) {
			addMessage( 2, "Client %i is already locked!", client );
			return client;
		}
		else {
			addMessage( 2, "Client %i is blocked by %i!", client, _curclient );
		}
		return -1;
	}
	addMessage( 1, "Locking %i!", client );
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
		addMessage( 1, "Unlocking %i", client );
		pthread_mutex_unlock( &_clientlock );
	}
	else if( _curclient != -1 ) {
		addMessage( 0, "Client %i is not %i", client, _curclient );
	}
	else {
		addMessage( 1, "Client %i was not locked!", client );
	}
}

static jsonObject *jsonAddProfiles( jsonObject *jo, const char *key, profile_t **vals, const int num ) {
	int i;
	jo = jsonInitArr(jo, key);
	for( i=0; i<num; i++ ) {
		jsonAddArrElement( jo, vals[i]->name, json_string );
	}
	return jo;
}

/*
 * helperfunction to add a title to the given jsonObject
 * if title is NULL an empty title will be created
 */
static jsonObject *jsonAddTitle( jsonObject *jo, const char *key, const mpplaylist_t *pl ) {
	jsonObject *val=NULL;
	mptitle_t *title=NULL;

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
		if( getDebug() > 1) {
			if(getFavplay()){
				jsonAddInt( val, "playcount", title->favpcount );
			}
			else {
				jsonAddInt( val, "playcount", title->playcount );
			}
			jsonAddInt( val, "skipcount", title->skipcount );
		}
	}
	else {
		val=jsonAddInt( NULL, "key", 0 );
		jsonAddStr( val, "artist", "Mixplay" );
		jsonAddStr( val, "album", "" );
		jsonAddStr( val, "title", getCurrentActivity() );
		jsonAddInt( val, "flags", 0 );
		jsonAddStr( val, "genre", "" );
	}

	return jsonAddObj(jo, key, val);
}

/*
 * helperfunction to fetch a title from the given jsonObject tree
 */
int jsonGetTitle( jsonObject *jo, const char *key, mptitle_t *title ) {
	assert( title != NULL );
	jsonObject *tjo=NULL;
	if( jsonPeek(jo, key) == json_error ) {
		title->key=0;
		strcpy(title->artist, "Mixplay");
		strcpy(title->album, "" );
		strcpy(title->title, "" );
		strcpy(title->display, "Mixplay");
		title->flags=0;
		strcpy(title->genre, "" );
		return 0;
	}
	else {
		tjo=jsonGetObj(jo, key);
		title->key=jsonGetInt(tjo, "key");
		jsonStrcpy(title->artist, tjo, "artist", NAMELEN);
		jsonStrcpy(title->album,  tjo, "album",  NAMELEN);
		jsonStrcpy(title->title,  tjo, "title",  NAMELEN);
		title->flags=jsonGetInt(tjo, "flags");
		jsonStrcpy(title->genre,  tjo, "genre",  NAMELEN);
		snprintf(title->display, MAXPATHLEN, "%s - %s", title->artist, title->title);
		title->playcount=jsonGetInt(tjo, "playcount");
		title->skipcount=jsonGetInt(tjo, "skipcount");
		return 1;
	}

	return -1;
}

/**
 * turns a playlist into a jsonObject.
 */
static jsonObject *jsonAddTitles( jsonObject *jo, const char *key, mpplaylist_t *pl, int dir ) {
	jsonObject *jsonTitle=NULL;

	jo=jsonInitArr( jo, key );
	while( pl != NULL ) {
		jsonTitle=jsonAddTitle( NULL, "", pl );
		jsonAddArrElement( jo, jsonTitle, json_object );
		if( dir < 0 ) {
			pl=pl->prev;
		}
		else {
			pl=pl->next;
		}
	}

	return jo;
}

static jsonObject *jsonAddList( jsonObject *jo, const char *key, marklist_t *list ) {
	jo = jsonInitArr( jo, key );
	while( list != NULL ) {
		jsonAddArrElement( jo, list->dir, json_string );
		list=list->next;
	}
	return jo;
}

/**
 * put data to be sent over into the buff
 * adds messages only if any are available for the client
**/
char *serializeStatus( unsigned long *count, int clientid, int type ) {
	mpconfig_t *data=getConfig();
	jsonObject *jo=NULL;
	mpplaylist_t *current=data->current;
	char *rv=NULL;
	char *err=NULL;
	const char *msgline;

	jo=jsonAddInt( jo, "type", type );

	if( type & MPCOMM_FULLSTAT ) {
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
	}
	if ( type & MPCOMM_RESULT ) {
		jsonAddTitles(jo, "titles", data->found->titles, 1 );
		jsonAddStrs(jo, "artists", data->found->artists, data->found->anum);
		jsonAddStrs(jo, "albums", data->found->albums, data->found->lnum);
		jsonAddStrs(jo, "albart", data->found->albart, data->found->lnum);
	}
	if ( type & MPCOMM_LISTS ) {
		jsonAddList( jo, "dnplist", data->dnplist );
		jsonAddList( jo, "favlist", data->favlist );
	}
	if ( type & MPCOMM_CONFIG ) {
		jsonAddInt( jo, "fade", data->fade );
		jsonAddStr( jo, "musicdir", data->musicdir );
		jsonAddProfiles( jo, "profile", data->profile, data->profiles );
		jsonAddInt( jo, "skipdnp", data->skipdnp );
		jsonAddStrs( jo, "stream", data->stream, data->streams );
		jsonAddStrs( jo, "sname", data->sname, data->streams );
	}
	jsonAddInt( jo, "active", data->active );
	jsonAddStr( jo, "playtime", data->playtime );
	jsonAddStr( jo, "remtime", data->remtime );
	jsonAddInt( jo, "percent", data->percent );
	jsonAddInt( jo, "volume", data->volume );
	jsonAddInt( jo, "status", data->status );
	jsonAddInt( jo, "mpmode", data->mpmode );
	jsonAddBool( jo, "mpfavplay", getFavplay() );
	jsonAddBool( jo, "fpcurrent", data->fpcurrent );

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
			/* alerts are disruptive */
			msgline=msgBuffPeek( data->msg, *count );
			if( strstr( msgline, "ALERT:" ) == msgline ) {
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

	err=jsonGetError(jo);
	if( err != NULL ) {
		addMessage(1,"%s",err);
		sfree(&err);
		jsonDiscard(jo);
		return NULL;
	}

	rv=jsonToString( jo );
	err=jsonGetError(jo);
	if( err != NULL ) {
		addMessage(1,"%s",err);
		sfree(&rv);
		sfree(&err);
		jsonDiscard(jo);
		return NULL;
	}

	jsonDiscard(jo);
	return rv;
}
