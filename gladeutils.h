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
typedef struct _MpData MpData;
struct _MpData {
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
};

/**
 * extension of mpcontrol->data
 */
struct glcontrol_t {
	MpData *widgets;			/* all (accessible) widgets */
    int fullscreen;				/* run in fullscreen mode */
    int debug;					/* debug log level (like verbose but print in requester) */
    char log[MP_LOGLEN];		/* debug log buffer */
};

/** defined in utils.h **/
/* void progressStart( const char *msg, ... ); */
/* #define progressLog( ... ) printver( 0, __VA_ARGS__ ) */
/* void progressLog( const char *msg, ... ); */
/* void progressEnd( const char *msg ); */
/* void updateUI( void *data ); */
/* void printver( int vl, const char *msg, ... ); */
/* void activity( const char *msg, ... ); */
/* void fail( int error, const char* msg, ... ); */
/** Not implemented / needed **/

/* void popUp( int level, const char *text, ... ); */
#endif /* __GLADEUTILS_H__ */
