#ifndef __GLADEUTILS_H__
#define __GLADEUTILS_H__

#include <glib.h>
#include <gtk/gtk.h>
#include <pthread.h>

#include "utils.h"
#include "musicmgr.h"

#define MP_LOGLEN 1024

/* Convenience macros for obtaining objects from UI file */
#define MP_GET_OBJECT( builder, name, type, data ) \
	data->name = type( gtk_builder_get_object( builder, #name ) )
#define MP_GET_WIDGET( builder, name, data ) \
	MP_GET_OBJECT( builder, name, GTK_WIDGET, data )

#define MP_GLDATA ((struct glcontrol_t *)getConfig()->data)

/* Main data structure definition */
typedef struct _MpData_t MpData;
struct _MpData_t {
	/* Widgets */
	GtkWidget *mixplay_main;
	GtkWidget *button_prev;
	GtkWidget *button_next;
	GtkWidget *title_current;
	GtkWidget *artist_current;
	GtkWidget *album_current;
	GtkWidget *genre_current;
	GtkWidget *button_play;
	GtkWidget *button_fav;
	GtkWidget *played;
	GtkWidget *remain;
	GtkWidget *progress;
	GtkWidget *button_profile;
	/* popup elements */
	GtkWidget *mp_popup;
	GtkWidget *volume;
};

/**
 * extension of mpconfig->data
 */
struct glcontrol_t {
	MpData *widgets;			/* all (accessible) widgets */
	int fullscreen;				/* run in fullscreen mode */
	struct msgbuf_t *msgbuff;	/* generic message buffer */
};

void g_progressHook( void * );
void g_updateHook( void * );

#endif /* __GLADEUTILS_H__ */
