#include <string.h>
#include <stdlib.h>

#include "gladeutils.h"
#include "player.h"

/*
 * mutex to block simultaneous access to dialog functions
 */
static pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;


static int g_activity( void *text ) {
    if ( MP_GLDATA->widgets->mp_popup != NULL ) {
        gtk_window_set_title( GTK_WINDOW( MP_GLDATA->widgets->mp_popup ), ( char * )text );
        gtk_widget_queue_draw( MP_GLDATA->widgets->mp_popup );
    }
    else if( MP_GLDATA->widgets->album_current != NULL ) {
        gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->album_current ),
                            text );
        gtk_widget_queue_draw( MP_GLDATA->widgets->mixplay_main );
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
        if( getConfig()->inUI ) {
        	gdk_threads_add_idle( g_activity, line );
        }
    }

    _ftrpos=( _ftrpos+1 )%400;
}

static int g_warn( void *line ) {
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
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
        if( getConfig()->inUI ) {
        	gdk_threads_add_idle( g_warn, line );
        }
        return;
    }
    else {
        fprintf( stderr, "FAIL: %s\n", line );
        if( getConfig()->inUI ) {
			if( F_FAIL == error ) {
				dialog = gtk_message_dialog_new ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
											  GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
											  "FAIL: %s", line );
			}
			else {
				dialog = gtk_message_dialog_new ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
											  GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
											  "%s\nERROR: %i - %s", line, abs( error ), strerror( abs( error ) ) );
			}

			gtk_dialog_run ( GTK_DIALOG ( dialog ) );
        }
        setCommand( mpc_quit );
    }

    return;
}

/**
 * actual gtk code for printver
 * is added as an idle thread to the main thread
 */
static int g_progressLog( void *line ) {
    pthread_mutex_lock( &msglock );
	scrollAdd( MP_GLDATA->log, line, MP_LOGLEN );
	if( NULL != MP_GLDATA->widgets->mp_popup ) {
		gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( MP_GLDATA->widgets->mp_popup ),
				"%s", MP_GLDATA->log );
		gtk_widget_queue_draw( MP_GLDATA->widgets->mp_popup );
	}
	else if( getConfig()->status != mpc_quit ) {
		fail( F_WARN, "No log window open for:\n%s", line );
	}
    free( line );
    pthread_mutex_unlock( &msglock );

    return 0;
}

/**
 * callback for the progress close button
 */
static void cb_progressClose( GtkDialog *dialog, gint res, gpointer data ) {
    /* make sure that nothing is added while we close the dialog */
    pthread_mutex_lock( &msglock );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
    MP_GLDATA->widgets->mp_popup=NULL;
    MP_GLDATA->log[0]='\0';
    pthread_mutex_unlock( &msglock );
}

/**
 * actual gtk code for prgressOpen
 * is added as an idle thread to the main thread
 */
static int g_progressStart( void *line ) {
    if( NULL == MP_GLDATA->widgets->mp_popup ) {
        MP_GLDATA->widgets->mp_popup = gtk_message_dialog_new ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                                       GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
                                       "%s", ( char * )line );
        gtk_dialog_add_button( GTK_DIALOG( MP_GLDATA->widgets->mp_popup ), "OK", 1 );
        g_signal_connect_swapped ( MP_GLDATA->widgets->mp_popup, "response",
                                   G_CALLBACK( cb_progressClose ),
                                   MP_GLDATA->widgets->mp_popup );
    }
    gtk_widget_set_sensitive( MP_GLDATA->widgets->mp_popup, FALSE );
    gtk_widget_show_all( MP_GLDATA->widgets->mp_popup );

    free( line );
    return 0;
}

/**
 * Opens an info requester
 * will be ignored if an info requester is already open
 */
void progressStart( char *msg, ... ) {
    va_list args;
    char *line;
    line=falloc( 512, sizeof( char ) );

    va_start( args, msg );
    vsnprintf( line, 512, msg, args );
    va_end( args );
    if( getConfig()->inUI ) {
		gdk_threads_add_idle( g_progressStart, line );

		while ( gtk_events_pending () ) {
			gtk_main_iteration ();
		}
    }
}

/**
 * Standard progress logging function
 * this is just another name for addMessage() and defined in gladeutils.h
 * just mentioned here for clarity
 */
/* #define progressLog( ... ) addMessage( 0, __VA_ARGS__ ) */

/**
 * actual gtk code for progressDone
 * is added as an idle thread to the main thread
 */
static int g_progressEnd( void *line ) {
    gtk_widget_set_sensitive( MP_GLDATA->widgets->mp_popup, TRUE );
    return g_progressLog( line );
}

/**
 * enables closing of the info requester
 */
void progressEnd( char *msg ) {
	char *line;

	if( NULL == MP_GLDATA->widgets->mp_popup ) {
        fail( F_WARN, "No progress request open!" );
    }

	line=falloc( 512, sizeof( char ) );

    if( NULL == msg ) {
        fail( F_WARN, "progressEnd() called with ZERO!" );
    	strncpy( line, "Done.\n", 512 );
    }
    else {
    	strncpy( line, msg, 512 );
    }
    if( getConfig()->inUI ) {
		gdk_threads_add_idle( g_progressEnd, line );

		while ( gtk_events_pending () ) {
			gtk_main_iteration ();
		}
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
    struct entry_t *runner;
    int i=0;
    char	buff[2*MAXPATHLEN];
    gboolean	usedb;
    control=( struct mpcontrol_t* )data;

    if( mpc_quit == control->command ) {
        addMessage( 2, "Already closing.." );
        return 0;
    }

    if( mpc_start == control->status ) {
    	activity( "Connecting" );
    	return 0;
    }

    if( MP_GLDATA->widgets->title_current == NULL ) {
        addMessage( 2, "No title widget yet.." );
        return 0;
    }

    /* these don't make sense when a stream is played */
    gtk_widget_set_visible( MP_GLDATA->widgets->button_next, !(control->playstream ) );
    gtk_widget_set_sensitive( MP_GLDATA->widgets->button_prev, !( control->playstream ) );
    gtk_widget_set_visible( MP_GLDATA->widgets->progress, !( control->playstream ) );
    gtk_widget_set_visible( MP_GLDATA->widgets->played, !( control->playstream ) );
    gtk_widget_set_visible( MP_GLDATA->widgets->remain, !( control->playstream ) );

    if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
        usedb=( control->root->key )?TRUE:FALSE;
        /* These depend on a database */
        gtk_widget_set_visible( MP_GLDATA->widgets->button_fav, usedb );

        gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->title_current ),
                            control->current->title );

        if( control->root->key != 0  ) {
        	infoLine( buff, control->current->plprev, MAXPATHLEN );
            gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_prev, buff );
        	infoLine( buff, control->current, MAXPATHLEN );
            gtk_widget_set_tooltip_text( MP_GLDATA->widgets->title_current, buff );
        }
        else {
            gtk_widget_set_tooltip_text( MP_GLDATA->widgets->title_current, control->current->path );
            gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_prev, NULL );
        }

        runner=control->current->plnext->plnext;
        while( ( runner != control->current ) && i < 5 ) {
        	if( i == 0 ) {
        		buff[0]=0;
        	}
			else {
				strcat( buff, "\n" );
			}
			strcat( buff, runner->display );
			runner=runner->plnext;
			i++;
        }
        gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_next, buff );

        gtk_widget_set_sensitive( MP_GLDATA->widgets->title_current, ( control->status == mpc_play ) );
        gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->artist_current ),
                            control->current->artist );
        gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->album_current ),
                            control->current->album );
        gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->genre_current ),
                            control->current->genre );
        setButtonLabel( MP_GLDATA->widgets->button_prev, control->current->plprev->display );

        if( MP_GLDATA->debug > 1 ) {
            sprintf( buff, "%2i %s", control->current->skipcount, control->current->plnext->display );
            setButtonLabel( MP_GLDATA->widgets->button_next, buff );
        }
        else {
            setButtonLabel( MP_GLDATA->widgets->button_next, control->current->plnext->display );
        }

        gtk_widget_set_sensitive( MP_GLDATA->widgets->button_fav, ( !( control->current->flags & MP_FAV ) ) );

        if( 0 == control->active ) {
           	if( control->playstream ) {
           	    gtk_widget_set_visible( MP_GLDATA->widgets->remain, 0 );
        		setButtonLabel( MP_GLDATA->widgets->button_profile, "Add Stream" );
           	}
           	else {
           	    gtk_widget_set_visible( MP_GLDATA->widgets->remain, -1 );
        		setButtonLabel( MP_GLDATA->widgets->button_profile, "Profile.." );
           	}
        }
        else {
    	    gtk_widget_set_visible( MP_GLDATA->widgets->remain, -1 );
    	    snprintf( buff, MAXPATHLEN, "Playing \n%s", (control->active < 0)?control->sname[-control->active-1]:control->profile[control->active-1] );
        	setButtonLabel( MP_GLDATA->widgets->button_profile, buff );
        }

        gtk_window_set_title ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                               control->current->display );
    }


    gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->played ),
                        control->playtime );
    gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->remain ),
                        control->remtime );
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( MP_GLDATA->widgets->progress ),
                                   control->percent/100.0 );

    return 0;
}

/**
 * gtk implementation of updateUI
 * fill the widgets with current information in the control data
 *
 * needed to keep updateUI() GUI independent
 */
void updateUI( mpconfig *control ) {
	if( control->inUI ) {
		gdk_threads_add_idle( g_updateUI, control );

		while ( gtk_events_pending () ) {
			gtk_main_iteration ();
		}
	}
}
