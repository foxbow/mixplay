#ifndef __GLADEUTILS_H__
#define __GLADEUTILS_H__

#include "musicmgr.h"
#include <gtk/gtk.h>

#define UI_FILE "gmixplay.glade"

#define MPCMD_IDLE 0
#define MPCMD_PLAY 1
#define MPCMD_PREV 2
#define MPCMD_NEXT 3
#define MPCMD_MDNP 4
#define MPCMD_MFAV 5
#define MPCMD_REPL 6

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
};

/**
 * holds the widgets and pipes for communication
 */
struct control_t {
	MpData *widgets;
	int p_status[2][2];
	int p_command[2][2];
	struct entry_t *root;
	struct entry_t *current;
//	char *status;
	int running;
	char *dbname;
	char *favname;
	char *dnpname;
	char playtime[10];
	char remtime[10];
	int percent;
	int stream;
	int command;
	int status;
};

void popUp( int time, const char *text, ... );
void drawframe( struct entry_t *current, const char *status, int stream );

#endif /* __GLADEUTILS_H__ */
