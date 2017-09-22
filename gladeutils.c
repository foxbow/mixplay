#include "player.h"
#include "gladeutils.h"
#include <string.h>
#include <stdlib.h>

/*
 * mutex to block simultaneous access to dialog functions
 */
static pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;

/*
 * Global mixplay control and data structure.
 */
extern struct mpcontrol_t *mpcontrol;

static int g_activity( void *text ) {
    if ( mpcontrol->widgets->mp_popup != NULL ) {
        gtk_window_set_title( GTK_WINDOW( mpcontrol->widgets->mp_popup ), ( char * )text );
        gtk_widget_queue_draw( mpcontrol->widgets->mp_popup );
    }
    else if( mpcontrol->widgets->album_current != NULL ) {
        gtk_label_set_text( GTK_LABEL( mpcontrol->widgets->album_current ),
                            text );
        gtk_widget_queue_draw( mpcontrol->widgets->mixplay_main );
    }

    free( text );
    return 0;
}

/**
 * activity indication
 */
static unsigned int _ftrpos=0;
void activity( const char *msg, ... ) {
    char roller[5]="|/-\\";
    char text[256]="";
    char *line;
    int pos;

    if( ( _ftrpos%100 ) == 0 ) {
        line=falloc( NAMELEN, sizeof( char ) );
        pos=( _ftrpos/100 )%4;

        va_list args;
        va_start( args, msg );
        vsprintf( text, msg, args );
        va_end( args );
        snprintf( line, NAMELEN, "%s %c", text, roller[pos] );
        gdk_threads_add_idle( g_activity, line );
    }

    _ftrpos=( _ftrpos+1 )%400;
}

static int g_warn( void *line ) {
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new ( GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                                      GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
                                      "%s", (char *)line );
    gtk_dialog_run ( GTK_DIALOG ( dialog ) );
	gtk_widget_destroy ( dialog );
	free(line);
	return 0;
}

/*
 * Show errormessage quit
 * msg - Message to print
 * error - errno that was set or 0 for no error to print
 */
void fail( int error, const char* msg, ... ) {
    va_list args;
    char *line;
    GtkWidget *dialog;

    line=falloc( 1024, sizeof(char) );

    va_start( args, msg );
    vsnprintf( line, 1024, msg, args );
    va_end( args );

    if( F_WARN == error ) {
        fprintf( stderr, "WARN: %s\n", line );
        gdk_threads_add_idle( g_warn, line );
        return;
    }
    else {
        fprintf( stderr, "FAIL: %s\n", line );
        dialog = gtk_message_dialog_new ( GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                                          GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                          "%s\nERROR: %i - %s", line, abs( error ), strerror( abs( error ) ) );

        gtk_dialog_run ( GTK_DIALOG ( dialog ) );
        setCommand(mpcontrol, mpc_quit );
    }

    return;
}

/**
 * actual gtk code for printver
 * is added as an idle thread to the main thread
 */
static int g_progressLog( void *line ) {
    pthread_mutex_lock( &msglock );
	scrollAdd( mpcontrol->log, line, MP_LOGLEN );
	if( NULL != mpcontrol->widgets->mp_popup ) {
		gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
				"%s", mpcontrol->log );
		gtk_widget_queue_draw( mpcontrol->widgets->mp_popup );
	}
	else if( mpcontrol->status != mpc_quit ) {
		fail( F_WARN, "No log window open for:\n%s", line );
	}
    free( line );
    pthread_mutex_unlock( &msglock );

    return 0;
}

/**
 * print the given message when the verbosity is at
 * least vl
 *
 * kind of threadsafe to keep messages in order and to avoid messing up the log buffer
 */
void printver( int vl, const char *msg, ... ) {
    va_list args;
    char *line;
    line=falloc( 512, sizeof( char ) );

    va_start( args, msg );
    vsnprintf( line, 512, msg, args );
    va_end( args );

    if( vl <= mpcontrol->debug ) {
        gdk_threads_add_idle( g_progressLog, line );
    }
    else if( vl <= getVerbosity() ) {
        fprintf( stderr, "%i: %s", vl, line );
        free( line );
    }
}

/**
 * callback for the progress close button
 */
static void cb_progressClose( GtkDialog *dialog, gint res, gpointer data ) {
    // make sure that nothing is added while we close the dialog
    pthread_mutex_lock( &msglock );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
    mpcontrol->widgets->mp_popup=NULL;
    mpcontrol->log[0]='\0';
    pthread_mutex_unlock( &msglock );
}

/**
 * actual gtk code for prgressOpen
 * is added as an idle thread to the main thread
 */
static int g_progressStart( void *line ) {
    if( NULL == mpcontrol->widgets->mp_popup ) {
        mpcontrol->widgets->mp_popup = gtk_message_dialog_new ( GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                                       GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
                                       "%s", ( char * )line );
        gtk_dialog_add_button( GTK_DIALOG( mpcontrol->widgets->mp_popup ), "OK", 1 );
        g_signal_connect_swapped ( mpcontrol->widgets->mp_popup, "response",
                                   G_CALLBACK( cb_progressClose ),
                                   mpcontrol->widgets->mp_popup );
    }
    gtk_widget_set_sensitive( mpcontrol->widgets->mp_popup, FALSE );
    gtk_widget_show_all( mpcontrol->widgets->mp_popup );

    free( line );
    return 0;
}

/**
 * Opens an info requester
 * will be ignored if an info requester is already open
 */
void progressStart( const char *msg, ... ) {
    va_list args;
    char *line;
    line=falloc( 512, sizeof( char ) );

    va_start( args, msg );
    vsnprintf( line, 512, msg, args );
    va_end( args );
    gdk_threads_add_idle( g_progressStart, line );

    while ( gtk_events_pending () ) {
        gtk_main_iteration ();
    }

}

/**
 * Standard progress logging function
 * this is just another name for printver() and defined in gladeutils.h
 * just mentioned here for clarity
 */
// #define progressLog( ... ) printver( 0, __VA_ARGS__ )

/**
 * actual gtk code for progressDone
 * is added as an idle thread to the main thread
 */
static int g_progressEnd( void *line ) {
    gtk_widget_set_sensitive( mpcontrol->widgets->mp_popup, TRUE );
    return g_progressLog( line );
}

/**
 * enables closing of the info requester
 */
void progressEnd( const char *msg ) {
	char *line;

	if( NULL == mpcontrol->widgets->mp_popup ) {
        fail( F_FAIL, "No progress request open!" );
    }

	line=falloc( 512, sizeof( char ) );

    if( NULL == msg ) {
        fail( F_WARN, "progressEnd() called with ZERO!" );
    	strncpy( line, "Done.\n", 512 );
    }
    else {
    	strncpy( line, msg, 512 );
    }
  	gdk_threads_add_idle( g_progressEnd, line );

    while ( gtk_events_pending () ) {
        gtk_main_iteration ();
    }
}

/**
 * wrapper to cut off text before setting the label
 */
static void setButtonLabel( GtkWidget *button, const char *text ) {
    char *label;
    label=falloc( NAMELEN, sizeof( char ) );
    strip( label, text, NAMELEN );
    gtk_button_set_label( GTK_BUTTON( button ), label );
    free( label );
}

/**
 * gather interesting stuff about a title, kind of a title.toString()
 */
static int infoLine( char *line, const struct entry_t *title, const int len ) {
	return snprintf( line, len, "%s\nKey: %04i - Fav: %s\nplaycount: %i (%s)\nskipcount: %i (%s)",
          title->path,
          title->key,
		  ONOFF( title->flags & MP_FAV ),
		  title->playcount,
		  ONOFF( ~( title->flags )&MP_CNTD ),
          title->skipcount,
          ONOFF( ~( title->flags )&MP_SKPD ) );
}

/**
 * This function is supposed to be called with gtk_add_thread_idle() to make update
 * work in multithreaded environments.
 */
static int g_updateUI( void *data ) {
    struct	mpcontrol_t *control;
    char	buff[MAXPATHLEN];
    gboolean	usedb;
    control=( struct mpcontrol_t* )data;

    if( mpc_quit == control->command ) {
        printver( 2, "Already closing..\n" );
        return 0;
    }

    if( mpc_start == control->status ) {
    	activity( "Connecting" );
    	return 0;
    }

    if( control->widgets->title_current == NULL ) {
        printver( 2, "No title widget yet..\n" );
        return 0;
    }

    // these don't make sense when a stream is played
    gtk_widget_set_visible( control->widgets->button_next, !( control->playstream ) );
    gtk_widget_set_sensitive( control->widgets->button_prev, !( control->playstream ) );
    gtk_widget_set_visible( control->widgets->progress, !( control->playstream ) );
    gtk_widget_set_visible( control->widgets->played, !( control->playstream ) );
    gtk_widget_set_visible( control->widgets->remain, !( control->playstream ) );

    if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
        usedb=( control->root->key )?TRUE:FALSE;
        // These depend on a database
        gtk_widget_set_visible( control->widgets->button_fav, usedb );

        gtk_label_set_text( GTK_LABEL( control->widgets->title_current ),
                            control->current->title );

        if( mpcontrol->root->key != 0  ) {
        	infoLine( buff, mpcontrol->current->plprev, MAXPATHLEN );
            gtk_widget_set_tooltip_text( mpcontrol->widgets->button_prev, buff );
        	infoLine( buff, mpcontrol->current, MAXPATHLEN );
            gtk_widget_set_tooltip_text( mpcontrol->widgets->title_current, buff );
        	infoLine( buff, mpcontrol->current->plnext, MAXPATHLEN );
            gtk_widget_set_tooltip_text( mpcontrol->widgets->button_next, buff );
        }
        else {
            gtk_widget_set_tooltip_text( mpcontrol->widgets->title_current, mpcontrol->current->path );
            gtk_widget_set_tooltip_text( mpcontrol->widgets->button_prev, NULL );
        }

        gtk_widget_set_sensitive( control->widgets->title_current, ( control->status == mpc_play ) );
        gtk_label_set_text( GTK_LABEL( control->widgets->artist_current ),
                            control->current->artist );
        gtk_label_set_text( GTK_LABEL( control->widgets->album_current ),
                            control->current->album );
        gtk_label_set_text( GTK_LABEL( control->widgets->genre_current ),
                            control->current->genre );
        setButtonLabel( control->widgets->button_prev, control->current->plprev->display );

        if( mpcontrol->debug > 1 ) {
            sprintf( buff, "%2i %s", control->current->skipcount, control->current->plnext->display );
            setButtonLabel( control->widgets->button_next, buff );
        }
        else {
            setButtonLabel( control->widgets->button_next, control->current->plnext->display );
        }

        gtk_widget_set_sensitive( control->widgets->button_fav, ( !( control->current->flags & MP_FAV ) ) );

        if( 0 == control->active ) {
           	if( control->playstream ) {
           	    gtk_widget_set_visible( control->widgets->remain, 0 );
        		setButtonLabel( control->widgets->button_profile, "Add Stream" );
           	}
           	else {
           	    gtk_widget_set_visible( control->widgets->remain, -1 );
        		setButtonLabel( control->widgets->button_profile, "Profile.." );
           	}
        }
        else {
    	    gtk_widget_set_visible( control->widgets->remain, -1 );
    	    snprintf( buff, MAXPATHLEN, "Playing \n%s", (control->active < 0)?control->sname[-control->active-1]:control->profile[control->active-1] );
        	setButtonLabel( control->widgets->button_profile, buff );
        }

        gtk_window_set_title ( GTK_WINDOW( control->widgets->mixplay_main ),
                               control->current->display );
    }


    gtk_label_set_text( GTK_LABEL( control->widgets->played ),
                        control->playtime );
    gtk_label_set_text( GTK_LABEL( control->widgets->remain ),
                        control->remtime );
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( control->widgets->progress ),
                                   control->percent/100.0 );

    return 0;
}

/**
 * gtk implementation of updateUI
 * fill the widgets with current information in the control data
 *
 * needed to keep updateUI() GUI independent
 */
void updateUI( void *control ) {
    gdk_threads_add_idle( g_updateUI, control );

    while ( gtk_events_pending () ) {
        gtk_main_iteration ();
    }
}
