/*
 * config.h
 *
 *  Created on: 16.11.2017
 *	  Author: bweber
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <pthread.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdbool.h>
#include "msgbuf.h"

#define MP_HOSTLEN 256
#define MP_MSGLEN 512
#define MAXCLIENT 100

/* standard crossfade value */
#define FADESECS 4

/*
 * commands and states
 * caveat: when changing this check *mpc_command[] in config.c too!
 */
typedef enum {
	mpc_unset = 0,
	mpc_play = 0,
	mpc_stop,
	mpc_prev,
	mpc_next,
	mpc_start,
	mpc_repl,
	mpc_profile,
	mpc_quit,
	mpc_dbclean,
	mpc_fav,
	mpc_dnp,					/* 10 */
	mpc_doublets,
	mpc_insert,
	mpc_ivol,
	mpc_dvol,
	mpc_fskip,
	mpc_bskip,					/* 0x10 */
	mpc_move,
	mpc_dbinfo,
	mpc_search,
	mpc_append,					/* 20 */
	mpc_setvol,
	mpc_newprof,
	mpc_path,
	mpc_remprof,
	mpc_smode,
	mpc_deldnp,
	mpc_delfav,
	mpc_remove,
	mpc_mute,
	mpc_favplay,				/* 30 */
	mpc_reset,
	mpc_pause,					/* 0x20 */
	mpc_clone,
	mpc_idle,
	/* by order of strength - fav-title beats dnp-album */
	mpc_genre = 1 << 8,			/* 0x0100 */
	mpc_artist = 1 << 9,		/* 0x0200 */
	mpc_album = 1 << 10,		/* 0x0400 */
	mpc_title = 1 << 11,		/* 0x0800 */
	mpc_display = 1 << 12,		/* 0x1000 */
	mpc_substr = 1 << 13,		/* 0x2000 */
	mpc_fuzzy = 1 << 14,		/* 0x4000 */
	mpc_mix = 1 << 14			/* 0x4000 */
} mpcmd_t;

/* some filtermasks */
#define MPC_DFRANGE  (mpc_genre|mpc_artist|mpc_album|mpc_title|mpc_display)
#define MPC_DNPFAV   (mpc_dnp|mpc_fav)
#define MPC_DFALL    (MPC_DFRANGE|MPC_DNPFAV)

#include "musicmgr.h"

/* default mixplay HTTP port */
#define MP_PORT 2347
#define PIDPATH "/tmp/mixplayd.pid"

/* commands that can be used on a remote control */
/* just use the key down event */
#define MPRC_SINGLE 7
/* use repoeated events */
#define MPRC_REPEAT 2
#define MPRC_NUM MPRC_SINGLE+MPRC_REPEAT

extern const mpcmd_t _mprccmds[MPRC_NUM];
extern const char *_mprccmdstrings[MPRC_NUM];

/*
 * qualifiers for mpc_dnp, mpc_fav and mpc_(long)search
 * 0FSR RRRR 000C CCCC
 */
/* extract raw command */
#define MPC_CMD(x)   (mpcmd_t)((int32_t)x&0x00ff)
/* determine range */
#define MPC_RANGE(x) (mpcmd_t)((int32_t)x&MPC_DFRANGE)
#define MPC_MODE(x)  (mpcmd_t)((int32_t)x&0xff00)
/* check for set range */
#define MPC_ISTITLE(x) ((x) & mpc_title)
#define MPC_ISARTIST(x) ((x) & mpc_artist)
#define MPC_ISALBUM(x) ((x) & mpc_album)
#define MPC_ISGENRE(x) ((x) & mpc_genre)
#define MPC_ISDISPLAY(x) ((x) & mpc_display)
/* check for exact range */
#define MPC_EQTITLE(x) (MPC_RANGE(x)==mpc_title)
#define MPC_EQARTIST(x) (MPC_RANGE(x)==mpc_artist)
#define MPC_EQALBUM(x) (MPC_RANGE(x)==mpc_album)
#define MPC_EQGENRE(x) (MPC_RANGE(x)==mpc_genre)
#define MPC_EQDISPLAY(x) (MPC_RANGE(x)==mpc_display)
/* shuffle enabled? */
#define MPC_ISSHUFFLE(x) ( x & mpc_mix )
/* shall it be a substring */
#define MPC_ISSUBSTR(x) (x & mpc_substr )
/* shall it be fuzzy */
#define MPC_ISFUZZY(x) (x & mpc_fuzzy )

/* playmodes */
#define PM_NONE     0x00
#define PM_STREAM   0x01
#define PM_UNUSED   0x02
#define PM_DATABASE 0x04
#define PM_SWITCH   0x08

/**
 * wrapper for streams and profiles
 */
typedef struct {
	char *name;					/* name to display */
	char *url;					/* URL to load or NULL for mix */
	int32_t volume;				/* last volume */
	uint32_t favplay;			/* favplay flag for mixes */
	uint32_t id;				/* unique id */
} profile_t;

/**
 * holds the widgets and pipes for communication
 * TODO this should be cut down some more
 */
typedef struct {
	char *musicdir;				/* path to the music */
	char *password;				/* password to lock up quit, scan and info */
	uint32_t active;			/* id of the current profile */
	uint32_t profiles;			/* number of profiles */
	profile_t **profile;		/* profiles */
	mptitle_t *root;			/* the root title */
	searchresults_t *found;		/* buffer list to contain searchresults etc */
	mpplaylist_t *current;		/* the current title */
	char *dbname;				/* path to the database */
	marklist_t *favlist;		/* favourites */
	marklist_t *dnplist;		/* DNPlist */
	marklist_t *dbllist;		/* doublets */
	uint32_t playtime;			/* seconds time into song */
	uint32_t remtime;			/* seconds remaining */
	int32_t percent;			/* how many percent of the song have been played */
	mpcmd_t status;				/* status of the player/system */
	pthread_t rtid;				/* thread ID of the reader */
	pthread_t stid;				/* thread ID of the server */
	uint32_t skipdnp;			/* how many skips mean dnp? */
	int32_t volume;				/* current volume [0..100] */
	char *channel;				/* the name of the ALSA master channel */
	int32_t debug;
	char *streamURL;			/* needed to load a new stream */
	msgbuf_t *msg;				/* generic message buffer */
	int32_t port;
	char *bookmarklet;			/* the code for the 'play' bookmarklet */
	uint32_t mpmode;			/* playmode, see PM_* */
	uint32_t sleepto;			/* idle timeout for clients */
	uint32_t dbDirty;
	/* flags for mpmode */
	uint32_t searchDNP:1;
	/* other flags */
	uint32_t fade;				/* controls fading between titles */
	uint32_t isDaemon:1;
	uint32_t inUI:1;			/* flag to show if the UI is active */
	uint32_t list:1;			/* remote playlist */
	uint32_t lineout:1;			/* enable line-out 100% volume */
	char *rcdev;				/* device by-id of the remote control */
	int32_t rccodes[MPRC_NUM];	/* command codes for the remote */
	uint32_t spread;
	uint32_t maxid;				/* highest profile id */
} mpconfig_t;

/* message request types */
/* return simple player status */
#define MPCOMM_STAT 0
/* return full title/playlist info */
#define MPCOMM_TITLES 1
/* return a search result */
#define MPCOMM_RESULT 2
/* return DNP and favlists */
#define MPCOMM_LISTS 4
/* return immutable configuration */
#define MPCOMM_CONFIG 8

void writeConfig(const char *musicpath);
mpconfig_t *readConfig(void);
mpconfig_t *getConfig(void);
mpconfig_t *createConfig(void);
void freeConfig(void);
void freeConfigContents(void);
int32_t getFavplay();
int32_t toggleFavplay();

profile_t *createProfile(const char *name, const char *stream,
						 const uint32_t favplay, const int32_t vol, bool id);
void freeProfile(profile_t * profile);
mpplaylist_t *getCurrent();
profile_t *getProfile(uint32_t id);
int32_t getProfileIndex(uint32_t id);
bool isStream(profile_t * profile);

void incDebug(void);
int32_t getDebug(void);
bool isDebug(void);
void addMessage(int32_t v, const char *msg, ...)
	__attribute__ ((__format__(__printf__, 2, 3)));

#define addError(x) addMessage( 0, "%i - %s", x, strerror(x) );

char *getCurrentActivity(void);

void activity(int32_t v, const char *msg, ...)
	__attribute__ ((__format__(__printf__, 2, 3)));

void updateUI(void);

void notifyChange(int32_t state);

void addProgressHook(void (*)(void *), void *id);
void addUpdateHook(void (*)());

const char *mpcString(mpcmd_t rawcmd);
char *fullpath(const char *file);

void wipePlaylist(mpconfig_t *);
void wipeSearchList(mpconfig_t *);

mptitle_t *wipeTitles(mptitle_t * root);
marklist_t *wipeList(marklist_t * root);
bool playerIsBusy(void);
void blockSigint();

int32_t getNotify(int32_t);
void addNotify(int32_t, int32_t);
void clearNotify(int32_t);

uint64_t getMsgCnt(int32_t);
void setMsgCnt(int32_t, uint64_t);
void incMsgCnt(int32_t);
void initMsgCnt(int32_t);

int32_t trylockPlaylist(void);
void lockPlaylist(void);
void unlockPlaylist(void);

#endif /* _CONFIG_H_ */
