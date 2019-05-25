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

#define MP_MSGLEN 512

/*
 * commands and states
 * caveat: when changing this check *mpc_command[] in config.c too!
 */
enum _mpcmd_t {
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
	mpc_QUIT,
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
};
typedef enum _mpcmd_t mpcmd;

#include "musicmgr.h"

/*
 * qualifiers for mpc_dnp, mpc_fav and mpc_(long)search
 * 000F RRRR CCCC CCCC
 */
/* extract raw command */
#define MPC_CMD(x)   (mpcmd)((int)x&0x00ff)
/* determine range */
#define MPC_RANGE(x) (mpcmd)((int)x&0xff00)
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

struct profile_t {
	char *name;
	unsigned favplay;
};

/**
 * holds the widgets and pipes for communication
 */
typedef struct _mpcontrol_t mpconfig;
struct _mpcontrol_t {
	char *musicdir;				/* path to the music */
	char pidpath[MAXPATHLEN];	/* path to the pidfile in demon mode */
	int active;					/* active >0 = profile / 0=none / <0 = stream */
	int profiles;				/* number of profiles */
	struct profile_t **profile;	/* profiles */
	int streams;				/* number of streams */
	char **stream;				/* stream URLs */
	char **sname;				/* stream names */
	mptitle *root;				/* the root title */
	searchresults *found;		/* buffer list to contain searchresults etc */
	mpplaylist *current;		/* the current title */
	char *dbname;				/* path to the database */
	struct marklist_t *favlist;	/* favourites */
	struct marklist_t *dnplist;	/* DNPlist */
	char playtime[20];			/* string containing time into song 00:00 */
	char remtime[20];			/* string containing remaining playtime 00:00 */
	int percent;				/* how many percent of the song have been played */
	mpcmd command;				/* command to the player */
	char *argument;				/* arguments to command */
	mpcmd status;					/* status of the player/system */
	pthread_t rtid;				/* thread ID of the reader */
	pthread_t stid;				/* thread ID of the server */
	int skipdnp;				/* how many skips mean dnp? */
	int volume;					/* current volume [0..100] */
	char *channel;				/* the name of the ALSA master channel */
	int verbosity;
	int debug;
	char *streamURL;
	struct msgbuf_t *msg;		/* generic message buffer */
	void *data;					/* extended data for gmixplay */
	char *host;
	int  port;
	unsigned playcount;
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
};

void writeConfig( const char *musicpath );
mpconfig *readConfig( void );
mpconfig *getConfig( void );
void freeConfig( void );
void freeConfigContents( void );
struct profile_t *getProfile();
struct profile_t *createProfile( const char *name, const unsigned favplay );
void freeProfile( struct profile_t *profile );

void incDebug( void );
int getDebug( void );
int getVerbosity( void );
int setVerbosity( int );
int incVerbosity( void );
void muteVerbosity( void );

void addMessage( int v, const char *msg, ... );
#define addError(x) addMessage( 0, "%i - %s", x, strerror(x) );
char *getMessage( void );

char *getCurrentActivity( void );
void activity( const char *msg, ... );

void progressStart( const char *msg, ... );
void progressEnd( void );
void progressMsg( const char *msg );
void updateUI( void );

void notifyChange();

void addNotifyHook( void (*)( void *), void *arg );
void addProgressHook( void (*)( void * ) );
void addUpdateHook( void (*)( void * ) );

void removeNotifyHook( void (*)( void *), void *arg );

const char *mpcString( mpcmd rawcmd );
mpcmd mpcCommand( const char *val );
int  setArgument( const char *arg );
int getArgs( int argc, char ** argv );
int initAll( void );
char *fullpath( const char *file );

#endif /* _CONFIG_H_ */
