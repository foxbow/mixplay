/*
 * config.h
 *
 *  Created on: 16.11.2017
 *      Author: bweber
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <pthread.h>
#include <sys/types.h>
#include "musicmgr.h"

#define MP_MSGLEN 512

/*
 * commands and states
 * changes here need to be reflected in the reader() code, namely in
 * char *mpc_command[]
 */
enum mpcmd_t {
    mpc_play,
    mpc_stop,
    mpc_prev,
    mpc_next,
    mpc_start,
    mpc_favtitle,
    mpc_favartist,
    mpc_favalbum,
    mpc_repl,
    mpc_profile,
    mpc_quit,
    mpc_dbclean,
    mpc_dnptitle,
    mpc_dnpartist,
    mpc_dnpalbum,
    mpc_dnpgenre,
	mpc_doublets,
	mpc_shuffle,
	mpc_ivol,
	mpc_dvol,
	mpc_fskip,
	mpc_bskip,
	mpc_QUIT,
    mpc_idle
};

typedef enum mpcmd_t mpcmd;

/**
 * holds the widgets and pipes for communication
 */
struct mpcontrol_t {
    char *musicdir;				/* path to the music */
    int profiles;				/* number of profiles */
    int active;		        	/* active >0 = profile / 0=none / <0 = stream */
    char **profile;				/* profile names */
    int streams;				/* number of streams */
    char **stream;				/* stream URLs */
    char **sname;				/* stream names */
    struct entry_t *root;		/* the root title */
    struct entry_t *current;	/* the current title */
    char *dbname;				/* path to the database */
    char *favname;				/* path to the favourites */
    char *dnpname;				/* path to the DNPlist */
    char playtime[10];			/* string containing time into song 00:00 */
    char remtime[10];			/* string containing remaining playtime 00:00 */
    int percent;				/* how many percent of the song have been played */
    int playstream;				/* profile or stream */
    mpcmd command;				/* command to the player */
    int status;					/* status of the player/system */
    pthread_t rtid;				/* thread ID of the reader */
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
};

typedef struct mpcontrol_t mpconfig;

mpconfig *writeConfig( const char *musicpath );
mpconfig *readConfig( void );
mpconfig *getConfig( void );
void freeConfig( void );

void incDebug( void );
int getDebug( void );
int getVerbosity( void );
int setVerbosity( int );
int incVerbosity( void );
void muteVerbosity( void );

void addMessage( int v, char *msg, ... );
int getMessage( char *msg );

const char *mpcString( mpcmd cmd );
const mpcmd mpcCommand( const char *val );
void setCommand( mpcmd cmd );

#endif /* _CONFIG_H_ */
