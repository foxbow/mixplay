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

static pthread_mutex_t _clientlock = PTHREAD_MUTEX_INITIALIZER;
static int32_t _curclient = -1;

#define MPV 10

/*
 * all of the next messages will only be sent to this client
 * other requests to set an exclusive client will be blocked
 * until the current client is unlocked
 *
 * Even if it may look tempting, queuing the calls is not
 * worth the effort!
 */
int32_t setCurClient(int32_t client) {
	if (pthread_mutex_trylock(&_clientlock) == EBUSY) {
		if (_curclient == client) {
			addMessage(MPV + 1, "Client %i is already locked!", client);
			return client;
		}
		else {
			addMessage(MPV + 1, "Client %i is blocked by %i!", client,
					   _curclient);
		}
		return -1;
	}
	addMessage(MPV + 1, "Locking %i!", client);
	_curclient = client;
	return client;
}

bool isCurClient(int32_t client) {
	return (_curclient == client);
}

int32_t getCurClient() {
	return _curclient;
}

/*
 * sends all next messages to every client and allows other clients
 * to lock the messages
 * only unlock if we really are the current client. Otherwise this is just a clean-up
 * call to avoid a deadlock.
 */
void unlockClient(int32_t client) {
	/* a little nasty but needed to end progress when it's unknown which client
	 * is the current one */
	if (client == -1) {
		client = _curclient;
	}

	if (client == _curclient) {
		_curclient = -1;
		addMessage(MPV + 1, "Unlocking %i", client);
		pthread_mutex_unlock(&_clientlock);
	}
	else if (_curclient != -1) {
		addMessage(0, "Client %i is not %i", client, _curclient);
	}
	else {
		addMessage(MPV + 1, "Client %i was not locked!", client);
	}
}

static jsonObject *jsonAddProfile(jsonObject * jo, const char *key,
								  const profile_t * profile) {
	jsonObject *val = NULL;

	val = jsonAddStr(NULL, "name", profile->name);
	jsonAddInt(val, "id", profile->id);
	jsonAddStr(val, "url", profile->url);

	return jsonAddObj(jo, key, val);
}

static jsonObject *jsonAddProfiles(jsonObject * jo, const char *key,
								   profile_t ** vals, const int32_t num) {
	int32_t i;

	jo = jsonInitArr(jo, key);
	for (i = 0; i < num; i++) {
		jsonObject *jsonProfile = jsonAddProfile(NULL, "profile", vals[i]);

		jsonAddArrElement(jo, jsonProfile, json_object);
	}
	return jo;
}

/*
 * helperfunction to add a title to the given jsonObject
 * if pl is NULL an empty title will be created
 */
static jsonObject *jsonAddTitle(jsonObject * jo, const char *key,
								const mpplaylist_t * pl) {
	jsonObject *val = NULL;
	mptitle_t *title = NULL;

	if (pl != NULL) {
		title = pl->title;
	}

	/* show activity if the playlist is empty, or the player is busy and
	 * the current title is to be added */
	if ((title == NULL) ||
		(playerIsBusy() && ((pl == getConfig()->current) || (pl == NULL)))) {
		val = jsonAddInt(NULL, "key", 0);
		jsonAddStr(val, "artist", "Mixplay");
		jsonAddStr(val, "album", "");
		jsonAddStr(val, "title", getCurrentActivity());
		jsonAddInt(val, "flags", 0);
		jsonAddStr(val, "genre", "");
		jsonAddInt(val, "favpcount", 0);
		jsonAddInt(val, "playcount", 0);
		jsonAddInt(val, "skipcount", 0);
		/* try again on next update */
		notifyChange(MPCOMM_TITLES);
	}
	else {
		val = jsonAddInt(NULL, "key", title->key);
		jsonAddStr(val, "artist", title->artist);
		jsonAddStr(val, "album", title->album);
		jsonAddStr(val, "title", title->title);
		jsonAddInt(val, "flags", title->flags);
		jsonAddStr(val, "genre", title->genre);
		jsonAddInt(val, "favpcount", title->favpcount);
		jsonAddInt(val, "playcount", title->playcount);
		jsonAddInt(val, "skipcount", title->skipcount);
	}
	return jsonAddObj(jo, key, val);
}

/**
 * turns a playlist into a jsonObject.
 * there may be a race condition when switching titles. It should be okay but
 * this needs to be checked over the time
 */
static jsonObject *jsonAddTitles(jsonObject * jo, const char *key,
								 mpplaylist_t * pl, int32_t dir) {
	jsonObject *jsonTitle = NULL;

	jo = jsonInitArr(jo, key);
	while (pl != NULL) {
		jsonTitle = jsonAddTitle(NULL, "title", pl);
		jsonAddArrElement(jo, jsonTitle, json_object);
		if (dir < 0) {
			pl = pl->prev;
		}
		else {
			pl = pl->next;
		}
	}

	return jo;
}

static jsonObject *jsonAddList(jsonObject * jo, const char *key,
							   marklist_t * list) {
	jo = jsonInitArr(jo, key);
	size_t len = strlen(getConfig()->musicdir) + 2;

	while (list != NULL) {
		/* cut off musicdir if it's given, list->dir starts with 'p=' */
		if (startsWith(list->dir, "p=")) {
			if (strlen(list->dir) < len) {
				addMessage(-1, "%s is an illegal list entry!", list->dir);
			}
			else {
				jsonAddArrElement(jo, list->dir + len, json_string);
			}
		}
		else
			jsonAddArrElement(jo, list->dir, json_string);
		list = list->next;
	}
	return jo;
}

/**
 * put data to be sent over into the buff
 * adds messages only if any are available for the client
**/
char *serializeStatus(int32_t clientid, int32_t type) {
	mpconfig_t *data = getConfig();
	jsonObject *jo = NULL;
	mpplaylist_t *current = data->current;
	char *rv = NULL;
	char *err = NULL;
	const clmessage *msg = NULL;
	char *msgline = NULL;

	jo = jsonAddInt(jo, "type", type);

	if (type & MPCOMM_TITLES) {
		jsonAddTitle(jo, "current", current);
		if (current && trylockPlaylist()) {
			jsonAddTitles(jo, "prev", current->prev, -1);
			jsonAddTitles(jo, "next", current->next, 1);
			unlockPlaylist();
		}
		else {
			jsonAddTitles(jo, "prev", NULL, -1);
			jsonAddTitles(jo, "next", NULL, 1);
			/* try again on next update */
			notifyChange(MPCOMM_TITLES);
		}
	}
	if (type & MPCOMM_RESULT) {
		jsonAddTitles(jo, "titles", data->found->titles, 1);
		jsonAddStrs(jo, "artists", data->found->artists, data->found->anum);
		jsonAddStrs(jo, "albums", data->found->albums, data->found->lnum);
		jsonAddStrs(jo, "albart", data->found->albart, data->found->lnum);
	}
	if (type & MPCOMM_LISTS) {
		jsonAddList(jo, "dnplist", data->dnplist);
		jsonAddList(jo, "favlist", data->favlist);
		jsonAddList(jo, "dbllist", data->dbllist);
	}
	if (type & MPCOMM_CONFIG) {
		jsonAddInt(jo, "fade", data->fade);
		jsonAddInt(jo, "lineout", data->lineout);
		jsonAddStr(jo, "musicdir", data->musicdir);
		jsonAddProfiles(jo, "profile", data->profile, data->profiles);
		jsonAddInt(jo, "skipdnp", data->skipdnp);
		jsonAddInt(jo, "sleepto", data->sleepto);
		jsonAddInt(jo, "debug", getDebug());
		jsonAddStr(jo, "bookmarklet",
				   data->bookmarklet ? data->bookmarklet : "");
	}
	jsonAddInt(jo, "active", data->active);
	jsonAddInt(jo, "playtime", data->playtime);
	jsonAddInt(jo, "remtime", data->remtime);
	jsonAddInt(jo, "percent", data->percent);
	jsonAddInt(jo, "volume", data->volume);
	jsonAddInt(jo, "status", data->status);
	jsonAddStr(jo, "mpstatus", mpcString(data->status));
	jsonAddInt(jo, "mpmode", data->mpmode);
	jsonAddBool(jo, "mpfavplay", getFavplay());
	jsonAddBool(jo, "searchDNP", data->searchDNP);
	jsonAddInt(jo, "clientid", clientid);
	/* broadcast */

	if (clientid > 0) {
		if (getMsgCnt(clientid) < data->msg->count) {
			msg = msgBuffPeek(data->msg, getMsgCnt(clientid));
			if (msg != NULL) {
				incMsgCnt(clientid);
				if ((msg->cid == clientid) || (msg->cid == -1)) {
					msgline = msg->msg;
				}
			}
		}
	}

	jsonAddStr(jo, "msg", msgline ? msgline : "");

	err = jsonGetError(jo);
	if (err != NULL) {
		addMessage(-1, "%s", err);
		sfree(&err);
		jsonDiscard(jo);
		return NULL;
	}

	rv = jsonToString(jo);
	err = jsonGetError(jo);
	if (err != NULL) {
		addMessage(-1, "%s", err);
		sfree(&rv);
		sfree(&err);
		jsonDiscard(jo);
		return NULL;
	}

	jsonDiscard(jo);
	return rv;
}

#undef MPV
