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
	mpc_shuffle,
	mpc_ivol,
	mpc_dvol,
	mpc_fskip,
	mpc_bskip,
	mpc_QUIT,
	mpc_dbinfo,
	mpc_search,
	mpc_longsearch,
	mpc_setvol,
	mpc_newprof,
	mpc_path,
	mpc_remprof,
	mpc_idle,
	mpc_title=1<<8,
	mpc_artist=2<<8,
	mpc_album=3<<8,
	mpc_genre=4<<8,
	mpc_display=5<<8,
	mpc_fuzzy=1<<12
};
typedef enum _mpcmd_t mpcmd;

#include "musicmgr.h"

/*
 * qualifiers for mpc_dnp, mpc_fav and mpc_(long)search
 * 000F RRRR CCCC CCCC
 */
/* extract raw command */
#define MPC_CMD(x)   (x&0x00ff)
/* determine range */
#define MPC_RANGE(x) (x&0x0f00)
#define MPC_ISTITLE(x) (MPC_RANGE(x)==mpc_title)
#define MPC_ISARTIST(x) (MPC_RANGE(x)==mpc_artist)
#define MPC_ISALBUM(x) (MPC_RANGE(x)==mpc_album)
#define MPC_ISGENRE(x) (MPC_RANGE(x)==mpc_genre)
#define MPC_ISDISPLAY(x) (MPC_RANGE(x)==mpc_display)
/* shall it be fuzzy */
#define MPC_ISFUZZY(x) (x & mpc_fuzzy )

/**
 * holds the widgets and pipes for communication
 */
typedef struct _mpcontrol_t mpconfig;
struct _mpcontrol_t {
	char *musicdir;				/* path to the music */
	int profiles;				/* number of profiles */
	int active;					/* active >0 = profile / 0=none / <0 = stream */
	char **profile;				/* profile names */
	int streams;				/* number of streams */
	char **stream;				/* stream URLs */
	char **sname;				/* stream names */
	mptitle *root;				/* the root title */
	mptitle *current;			/* the current title */
	char *dbname;				/* path to the database */
	char *favname;				/* path to the favourites */
	char *dnpname;				/* path to the DNPlist */
	char playtime[10];			/* string containing time into song 00:00 */
	char remtime[10];			/* string containing remaining playtime 00:00 */
	int percent;				/* how many percent of the song have been played */
	int playstream;				/* profile or stream */
	mpcmd command;				/* command to the player */
	char *argument;				/* arguments to command */
	int status;					/* status of the player/system */
	pthread_t rtid;				/* thread ID of the reader */
	pthread_t stid;				/* thread ID of the server */
	int skipdnp;				/* how many skips mean dnp? */
	int fade;					/* controls fading between titles */
	int volume;					/* current volume [0..100] */
	char *channel;				/* the name of the ALSA master channel */
	int verbosity;
	int debug;
	struct msgbuf_t *msg;		/* generic message buffer */
	int inUI;					/* flag to show if the UI is active */
	void *data;					/* extended data for gmixplay */
	char *host;
	int  port;
	int remote;
	int changed;
	int isDaemon;
};

void writeConfig( const char *musicpath );
mpconfig *readConfig( void );
mpconfig *getConfig( void );
void freeConfig( void );
void freeConfigContents( void );

void incDebug( void );
int getDebug( void );
int getVerbosity( void );
int setVerbosity( int );
int incVerbosity( void );
void muteVerbosity( void );

void addMessage( int v, char *msg, ... );
char *getMessage( void );

void progressStart( char *msg, ... );
void progressEnd( void );
void addProgressHook( void (*func)( void *arg ) );
void addUpdateHook( void (*func)( void *arg ) );
void updateUI( void );

const char *mpcString( mpcmd rawcmd );
const mpcmd mpcCommand( const char *val );
void setCommand( mpcmd rawcmd );
int getArgs( int argc, char ** argv );
int initAll( int );
#endif /* _CONFIG_H_ */
