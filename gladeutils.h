#ifndef __GLADEUTILS_H__
#define __GLADEUTILS_H__

#include "musicmgr.h"
#include <glib.h>
#include <gtk/gtk.h>

// command and status values
#define MPCMD_IDLE	0
#define MPCMD_PLAY	1
#define MPCMD_PREV	2
#define MPCMD_NEXT	3
#define MPCMD_MDNP	4
#define MPCMD_MFAV	5
#define MPCMD_REPL	6
#define MPCMD_NXTP	7
#define MPCMD_QUIT	8
#define MPCMD_DBSCAN	9
#define MPCMD_DBNEW		10
#define MPCMD_WARN	11
#define MPCMD_ERR	12

/* Convenience macros for obtaining objects from UI file */
#define MP_GET_OBJECT( builder, name, type, data ) \
    data->name = type( gtk_builder_get_object( builder, #name ) )
#define MP_GET_WIDGET( builder, name, data ) \
    MP_GET_OBJECT( builder, name, GTK_WIDGET, data )

/* Main data structure definition */
typedef struct _MpData MpData;
struct _MpData
{
	/* Widgets */
	GtkWidget *mixplay_main;
	GtkWidget *button_prev;
	GtkWidget *button_next;
	GtkWidget *button_replay;
	GtkWidget *image_current;
	GtkWidget *title_current;
	GtkWidget *artist_current;
	GtkWidget *album_current;
	GtkWidget *genre_current;
	GtkWidget *displayname_prev;
	GtkWidget *displayname_next;
	GtkWidget *button_dnp;
	GtkWidget *button_play;
	GtkWidget *button_fav;
	GtkWidget *played;
	GtkWidget *remain;
	GtkWidget *progress;
//	GtkWidget *checkmark;
	GtkWidget *pause;
	GtkWidget *play;
	GtkWidget *down;
	GtkWidget *skip;
	GtkWidget *noentry;
	GtkWidget *menu_profiles;
	GtkWidget *menu_streams;
	GtkWidget *button_profile;
	GtkWidget *menu_profile[5];
};

/**
 * holds the widgets and pipesnoentry for communication
 */
struct mpcontrol_t {
	MpData *widgets;			// all (accessible) widgets
	char *musicdir;				// path to the music
	gsize profiles;				// number of profiles
	unsigned long active;		// active profile
	char **profile;				// profile names
	gsize streams;				// number of streams
	char **stream;				// stream URLs
	char **sname;				// stream names
	int p_status[2][2];			// status pipes to mpg123
	int p_command[2][2];		// command pipes to mog123
	struct entry_t *root;		// the root title
	struct entry_t *current;	// the current title
	char *dbname;				// path to the database
	char *favname;				// path to the favourites
	char *dnpname;				// path to the DNPlist
	char playtime[10];			// string containing time into song 00:00
	char remtime[10];			// string containing remaining playtime 00:00
	int percent;				// how many percent of the song have been played
	int playstream;				// playlist or stream
	int command;				// command to the player
	int status;					// status of the player/system
	pthread_t rtid;				// thread ID of the reader
};

// void fail( int error, const char* msg, ... );
void popUp( int time, const char *text, ... );
#endif /* __GLADEUTILS_H__ */
