/*
 * player.h
 *
 *  Created on: 26.04.2017
 *      Author: bweber
 */

#ifndef PLAYER_H_
#define PLAYER_H_
#include "gladeutils.h"

#define MP_LOGLEN 1024

enum mpcmd_t {
    mpc_idle,
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
    mpc_dnpgenre
};

typedef enum mpcmd_t mpcmd;

/**
 * holds the widgets and pipesnoentry for communication
 */
struct mpcontrol_t {
    MpData *widgets;			// all (accessible) widgets
    char *musicdir;				// path to the music
    gsize profiles;				// number of profiles
    unsigned long active;		// active profile/stream
    char **profile;				// profile names
    gsize streams;				// number of streams
    char **stream;				// stream URLs
    char **sname;				// stream names
    int p_status[2][2];			// status pipes to mpg123
    int p_command[2][2];		// command pipes to mpg123
    struct entry_t *root;		// the root title
    struct entry_t *current;	// the current title
    char *dbname;				// path to the database
    char *favname;				// path to the favourites
    char *dnpname;				// path to the DNPlist
    char playtime[10];			// string containing time into song 00:00
    char remtime[10];			// string containing remaining playtime 00:00
    int percent;				// how many percent of the song have been played
    int playstream;				// profile or stream
    mpcmd command;				// command to the player
    int status;					// status of the player/system
    pthread_t rtid;				// thread ID of the reader
    char log[MP_LOGLEN];
    int fullscreen;
    int debug;
};

void setCommand( struct mpcontrol_t *control, mpcmd cmd );
void *reader( void *cont );
void *setProfile( void *data );

#endif /* PLAYER_H_ */
