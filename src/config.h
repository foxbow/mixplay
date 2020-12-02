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
#include "msgbuf.h"

#define MP_MSGLEN 512
#define MAXCLIENT 100
#define STREAM_TIMEOUT 9

/*
 * commands and states
 * caveat: when changing this check *mpc_command[] in config.c too!
 */
typedef enum {
	mpc_play=0,
	mpc_stop,
	mpc_prev,
	mpc_next,
	mpc_start,
	mpc_repl,
	mpc_profile,
	mpc_quit,
	mpc_dbclean,
	mpc_fav,
	mpc_dnp,           /* 10 */
	mpc_doublets,
	mpc_insert,
	mpc_ivol,
	mpc_dvol,
	mpc_fskip,
	mpc_bskip,         /* 0x10 */
	mpc_move,
	mpc_dbinfo,
	mpc_search,
	mpc_append,        /* 20 */
	mpc_setvol,
	mpc_newprof,
	mpc_path,
	mpc_remprof,
	mpc_edit,
	mpc_deldnp,
	mpc_delfav,
	mpc_remove,
	mpc_mute,
	mpc_favplay,       /* 30 */
	mpc_reset,
	mpc_idle,          /* 0x20 */
	mpc_title=1<<8,    /* 0x0100 */
	mpc_artist=1<<9,   /* 0x0200 */
	mpc_album=1<<10,   /* 0x0400 */
	mpc_display=1<<11, /* 0x0800 */
	mpc_genre=1<<12,   /* 0x1000 */
	mpc_substr=1<<13,  /* 0x2000 */
	mpc_fuzzy=1<<14,   /* 0x4000 */
	mpc_mix=1<<14      /* 0x4000 */
} mpcmd_t;

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
const char *_mprccmdstrings[MPRC_NUM];

/*
 * qualifiers for mpc_dnp, mpc_fav and mpc_(long)search
 * 0FSR RRRR 000C CCCC
 */
/* extract raw command */
#define MPC_CMD(x)   (mpcmd_t)((int)x&0x00ff)
/* determine range */
#define MPC_RANGE(x) (mpcmd_t)((int)x&0x1f00)
#define MPC_MODE(x)  (mpcmd_t)((int)x&0xff00)
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
#define PM_PLAYLIST 0x02
#define PM_DATABASE 0x04
#define PM_SWITCH   0x08

typedef struct {
	char *name;
	unsigned favplay;
} profile_t ;

/**
 * holds the widgets and pipes for communication
 */
typedef struct {
	char *musicdir;				/* path to the music */
	char *password;				/* password to lock up quit, scan and info */
	int active;					/* active >0 = profile / 0=none / <0 = stream */
	int profiles;				/* number of profiles */
	profile_t **profile;	/* profiles */
	int streams;				/* number of streams */
	char **stream;				/* stream URLs */
	char **sname;				/* stream names */
	mptitle_t *root;				/* the root title */
	searchresults_t *found;		/* buffer list to contain searchresults etc */
	mpplaylist_t *current;		/* the current title */
	char *dbname;				/* path to the database */
	marklist_t *favlist;	/* favourites */
	marklist_t *dnplist;	/* DNPlist */
	marklist_t *dbllist;	/* doublets */
	unsigned playtime;			/* seconds time into song */
	unsigned remtime;			/* seconds remaining */
	int percent;				/* how many percent of the song have been played */
	mpcmd_t command;				/* command to the player */
	char *argument;				/* arguments to command */
	mpcmd_t status;					/* status of the player/system */
	pthread_t rtid;				/* thread ID of the reader */
	pthread_t stid;				/* thread ID of the server */
	unsigned skipdnp;				/* how many skips mean dnp? */
	int volume;					/* current volume [0..100] */
	char *channel;				/* the name of the ALSA master channel */
	int debug;
	char *streamURL;
	msgbuf_t *msg;		/* generic message buffer */
	int  port;
	unsigned mpmode;						/* playmode, see PM_* */
	unsigned sleepto;			/* idle timeout for clients */
	unsigned dbDirty;
	/* flags for mpmode */
	unsigned fpcurrent:1;
	unsigned mpmix:1;
	/* other flags */
	unsigned fade:1;					/* controls fading between titles */
	unsigned isDaemon:1;
	unsigned inUI:1;					/* flag to show if the UI is active */
	unsigned list:1;					/* remote playlist */
	char *rcdev;							/* device by-id of the remote control */
	int rccodes[MPRC_NUM];		/* command codes for the remote */
	unsigned client[MAXCLIENT];		/* glabal clientID marker */
	unsigned notify[MAXCLIENT];		/* next state per client */
	unsigned long msgcnt[MAXCLIENT];
	unsigned watchdog;
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

void writeConfig( const char *musicpath );
mpconfig_t *readConfig( void );
mpconfig_t *getConfig( void );
mpconfig_t *createConfig( void );
void freeConfig( void );
void freeConfigContents( void );
int getFavplay();
int toggleFavplay();
profile_t *createProfile( const char *name, const unsigned favplay );
void freeProfile( profile_t *profile );

void incDebug( void );
int getDebug( void );
void addMessage( int v, const char *msg, ... ) __attribute__((__format__(__printf__, 2, 3)));
#define addError(x) addMessage( 0, "%i - %s", x, strerror(x) );

char *getCurrentActivity( void );
void activity( int v, const char *msg, ... ) __attribute__((__format__(__printf__, 2, 3)));

void updateUI( void );

void notifyChange(int state);

void addProgressHook( void (*)( void * ), void *id );
void addUpdateHook( void (*)( ) );

const char *mpcString( mpcmd_t rawcmd );
mpcmd_t mpcCommand( const char *val );
char *fullpath( const char *file );

mpplaylist_t *wipePlaylist( mpplaylist_t *pl );
mptitle_t *wipeTitles( mptitle_t *root );
marklist_t *wipeList( marklist_t *root );
int playerIsInactive( void );
void blockSigint();

int getFreeClient( void );
void freeClient( int );
int trylockClient( int );

int getNotify( int );
void setNotify( int, int );
void addNotify( int, int );

unsigned long getMsgCnt( int );
void setMsgCnt( int, unsigned long );
void incMsgCnt( int );
void initMsgCnt( int );

void lockPlaylist( void );
void unlockPlaylist( void );
int trylockPlaylist( void );

#endif /* _CONFIG_H_ */
