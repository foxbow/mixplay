#ifndef __GLADEUTILS_H__
#define __GLADEUTILS_H__

#include "utils.h"
#include "musicmgr.h"

#include <glib.h>
#include <gtk/gtk.h>
#include <pthread.h>

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
	GtkWidget *title_current;
	GtkWidget *artist_current;
	GtkWidget *album_current;
	GtkWidget *genre_current;
	GtkWidget *button_play;
	GtkWidget *button_fav;
	GtkWidget *played;
	GtkWidget *remain;
	GtkWidget *progress;
	// popup elements
	GtkWidget *mp_popup;
};


#define progressAdd( ... ) printver( 0, __VA_ARGS__ )
void progressLog( const char *msg, ... );
void progressDone( const char *msg );
void updateUI( void *data );
/** defined in utils.h **/
// void fail( int error, const char* msg, ... );
// void popUp( int level, const char *text, ... );
#endif /* __GLADEUTILS_H__ */
