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
	mpc_dnp,
	mpc_doublets,
	mpc_insert,
	mpc_ivol,
	mpc_dvol,
	mpc_fskip,
	mpc_bskip,
	mpc_move,
	mpc_dbinfo,
	mpc_search,
	mpc_append,
	mpc_setvol,
	mpc_newprof,
	mpc_path,
	mpc_remprof,
	mpc_edit,
	mpc_deldnp,
	mpc_delfav,
	mpc_remove,
	mpc_mute,
	mpc_favplay,
	mpc_idle,
	mpc_title=1<<8,
	mpc_artist=1<<9,
	mpc_album=1<<10,
	mpc_display=1<<11,
	mpc_genre=1<<12,
	mpc_fuzzy=1<<13,
	mpc_mix=1<<13
} mpcmd_t;

#include "musicmgr.h"

/* commands that can be used on a remote control */
/* just use the key down event */
#define MPRC_SINGLE 6
/* use repoeated events */
#define MPRC_REPEAT 4
#define MPRC_NUM MPRC_SINGLE+MPRC_REPEAT

extern const mpcmd_t _mprccmds[MPRC_NUM];


/*
 * qualifiers for mpc_dnp, mpc_fav and mpc_(long)search
 * 000F RRRR CCCC CCCC
 */
/* extract raw command */
#define MPC_CMD(x)   (mpcmd_t)((int)x&0x00ff)
/* determine range */
#define MPC_RANGE(x) (mpcmd_t)((int)x&0xff00)
#define MPC_ISTITLE(x) (x & mpc_title)
#define MPC_ISARTIST(x) (x & mpc_artist)
#define MPC_ISALBUM(x) ( x & mpc_album)
#define MPC_ISGENRE(x) ( x & mpc_genre)
#define MPC_ISDISPLAY(x) ( x & mpc_display)
#define MPC_EQTITLE(x) (MPC_RANGE(x)==mpc_title)
#define MPC_EQARTIST(x) (MPC_RANGE(x)==mpc_artist)
#define MPC_EQALBUM(x) (MPC_RANGE(x)==mpc_album)
#define MPC_EQGENRE(x) (MPC_RANGE(x)==mpc_genre)
#define MPC_EQDISPLAY(x) (MPC_RANGE(x)==mpc_display)
#define MPC_ISSHUFFLE(x) ( x & mpc_mix )
/* shall it be fuzzy */
#define MPC_ISFUZZY(x) (x & mpc_fuzzy )

/* playmodes */
#define PM_NONE     0x00
#define PM_STREAM   0x01
#define PM_PLAYLIST 0x02
#define PM_DATABASE 0x03

typedef struct {
	char *name;
	unsigned favplay;
} profile_t ;

/**
 * holds the widgets and pipes for communication
 */
typedef struct {
	char *musicdir;				/* path to the music */
	char pidpath[MAXPATHLEN];	/* path to the pidfile in demon mode */
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
	char playtime[20];			/* string containing time into song 00:00 */
	char remtime[20];			/* string containing remaining playtime 00:00 */
	int percent;				/* how many percent of the song have been played */
	mpcmd_t command;				/* command to the player */
	char *argument;				/* arguments to command */
	mpcmd_t status;					/* status of the player/system */
	pthread_t rtid;				/* thread ID of the reader */
	pthread_t stid;				/* thread ID of the server */
	unsigned skipdnp;				/* how many skips mean dnp? */
	int volume;					/* current volume [0..100] */
	char *channel;				/* the name of the ALSA master channel */
	int verbosity;
	int debug;
	char *streamURL;
	msgbuf_t *msg;		/* generic message buffer */
	void *data;					/* extended data for gmixplay */
	int  port;
	int mpmode;						/* playmode, see PM_* */
	unsigned dbDirty;
	/* flags for mpmode */
	unsigned mpedit:1;
	unsigned mpmix:1;
	/* other flags */
	unsigned fade:1;					/* controls fading between titles */
	unsigned isDaemon:1;
	unsigned inUI:1;					/* flag to show if the UI is active */
	unsigned changed:1;
	unsigned listDirty:1;
	char *rcdev;							/* device by-id of the remote control */
	int rccodes[MPRC_NUM];		/* command codes for the remote */
} mpconfig_t;

void writeConfig( const char *musicpath );
mpconfig_t *readConfig( void );
mpconfig_t *getConfig( void );
mpconfig_t *createConfig( void );
void freeConfig( void );
void freeConfigContents( void );
profile_t *getProfile();
profile_t *createProfile( const char *name, const unsigned favplay );
void freeProfile( profile_t *profile );

void incDebug( void );
int getDebug( void );
int getVerbosity( void );
int setVerbosity( int );
int incVerbosity( void );
void muteVerbosity( void );
void addMessage( int v, const char *msg, ... ) __attribute__((__format__(__printf__, 2, 3)));
#define addError(x) addMessage( 0, "%i - %s", x, strerror(x) );
char *getMessage( void );

char *getCurrentActivity( void );
void activity( const char *msg, ... ) __attribute__((__format__(__printf__, 1, 2)));

void progressStart( const char *msg, ... ) __attribute__((__format__(__printf__, 1, 2)));
void progressEnd( void );
void progressMsg( const char *msg );
void updateUI( void );

void notifyChange();

void addNotifyHook( void (*)( void *), void *arg );
void addProgressHook( void (*)( void * ), void *id );
void addUpdateHook( void (*)( void * ) );

void removeNotifyHook( void (*)( void *), void *arg );

const char *mpcString( mpcmd_t rawcmd );
mpcmd_t mpcCommand( const char *val );
char *fullpath( const char *file );

mpplaylist_t *wipePlaylist( mpplaylist_t *pl );
mptitle_t *wipeTitles( mptitle_t *root );
marklist_t *wipeList( marklist_t *root );

#endif /* _CONFIG_H_ */
