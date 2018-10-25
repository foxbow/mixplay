#include <string.h>
#include <stdlib.h>

#include "gladeutils.h"
#include "player.h"

/*
 * mutex to block simultaneous access to dialog functions
 */
static pthread_mutex_t gmsglock=PTHREAD_MUTEX_INITIALIZER;

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
		else {
			free( line );
		}
	}

	_ftrpos=( _ftrpos+1 )%400;
}

/*
 * Show errormessage quit
 * msg - Message to print
 * error - errno that was set or 0 for no error to print
 */
void fail( const int error, const char* msg, ... ) {
	va_list args;
	char *line;
	GtkWidget *dialog;

	line=falloc( 1024, sizeof(char) );

	va_start( args, msg );
	vsnprintf( line, 1024, msg, args );
	va_end( args );

	setCommand(mpc_quit);
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
		gtk_main_quit();
	}

	exit( EXIT_FAILURE );
}

/**
 * callback for the progress close button
 */
static void cb_progressClose( GtkDialog *dialog, gint res, gpointer data ) {
	/* make sure that nothing is added while we close the dialog */
	pthread_mutex_lock( &gmsglock );
	gtk_widget_destroy( GTK_WIDGET( dialog ) );
	MP_GLDATA->widgets->mp_popup=NULL;
	msgBuffClear( MP_GLDATA->msgbuff );
	pthread_mutex_unlock( &gmsglock );
	if( getConfig()->status == mpc_quit ) {
		gtk_main_quit();
	}
}

/**
 * actual gtk code for progressOpen
 * is added as an idle thread to the main thread
 */
static int g_progressStart( void *title ) {
	if( NULL == MP_GLDATA->widgets->mp_popup ) {
		MP_GLDATA->widgets->mp_popup = gtk_message_dialog_new ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
									   GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
									   "%s", ( char * )title );
		gtk_dialog_add_button( GTK_DIALOG( MP_GLDATA->widgets->mp_popup ), "OK", 1 );
		g_signal_connect_swapped ( MP_GLDATA->widgets->mp_popup, "response",
								   G_CALLBACK( cb_progressClose ),
								   MP_GLDATA->widgets->mp_popup );
	}
	gtk_widget_set_sensitive( MP_GLDATA->widgets->mp_popup, FALSE );
	gtk_widget_show_all( MP_GLDATA->widgets->mp_popup );
	free( title );
	return 0;
}

/**
 * pops up a requester to show a message
 * Usually called from within an ProgressStart() ProgressEnd() bracket
 * Can be called as is and just pops up a closeable requester
 */
static int g_progressLog( void *line ) {
	char *msg;
	char *title=NULL;

	if( line == NULL ) return 0;

	pthread_mutex_lock( &gmsglock );
	msgBuffAdd( MP_GLDATA->msgbuff, line);
	msg=msgBuffAll( MP_GLDATA->msgbuff );

	if( NULL == MP_GLDATA->widgets->mp_popup ) {
		/* The app is cleaning up but has not yet left the main loop */
		if( getConfig()->status == mpc_quit ) {
				fprintf( stderr, "** %s\n", (char *)line );
		}
		else {
			/* Use a temporary string as g_progressStart free()s the parameter! */
			title=falloc( strlen("Info:")+1, sizeof( char ) );
			strcpy( title, "Info:" );
			g_progressStart( title );
			gtk_widget_set_sensitive( MP_GLDATA->widgets->mp_popup, TRUE );
		}
	}

	if( NULL != MP_GLDATA->widgets->mp_popup ) {
		gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( MP_GLDATA->widgets->mp_popup ),
				"%s", msg );
		gtk_widget_queue_draw( MP_GLDATA->widgets->mp_popup );
	}

	free( msg );
	free( line );
	pthread_mutex_unlock( &gmsglock );

	return 0;
}

/**
 * actual gtk code for progressDone
 * is added as an idle thread to the main thread
 */
static int g_progressEnd( void *data ) {
	gtk_widget_set_sensitive( MP_GLDATA->widgets->mp_popup, TRUE );
	return 0;
}

/**
 * extended progress handling for gmixplay
 */
void g_progressHook( void *msg ) {
	if( msg == NULL ) {
		if( getConfig()->inUI ) {
			gdk_threads_add_idle( g_progressEnd, NULL );

			while ( gtk_events_pending () ) {
				gtk_main_iteration ();
			}
		}
	}
	else {
		if( getConfig()->inUI ) {
			gdk_threads_add_idle( g_progressStart, (void *)msg );

			while ( gtk_events_pending () ) {
				gtk_main_iteration ();
			}
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
static int infoLine( char *line, const mptitle *title, const int len ) {
	if( title->key != 0 ) {
		return snprintf( line, len, "%s\nKey: %04i - Fav: %s\nplaycount: %i (%s)\nskipcount: %i (%s)",
			  title->path,
			  title->key,
			  ONOFF( title->flags & MP_FAV ),
			  title->playcount,
			  ONOFF( ~( title->flags )&MP_CNTD ),
			  title->skipcount,
			  ONOFF( ~( title->flags )&MP_SKPD ) );
	}
	else {
		return snprintf( line, len, "%s\nFav: %s\nplaycount: %i (%s)\nskipcount: %i (%s)",
			  title->path,
			  ONOFF( title->flags & MP_FAV ),
			  title->playcount,
			  ONOFF( ~( title->flags )&MP_CNTD ),
			  title->skipcount,
			  ONOFF( ~( title->flags )&MP_SKPD ) );
	}
}

/**
 * This function is supposed to be called with gtk_add_thread_idle() to make update
 * work in multithreaded environments.
 *
 * todo: this needs to be cleaned up!
 */
static int g_updateUI( void *data ) {
	mpconfig *control;
	mpplaylist *runner;
	int i=0;
	char	buff[2*MAXPATHLEN];
	gboolean	usedb;
	control=( mpconfig * )data;

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

	/* do we have volume control? */
	gtk_widget_set_visible( MP_GLDATA->widgets->volume, (control->volume > -1) );

	if( ( NULL != control->current ) &&
			(control->current->title != NULL) &&
			( 0 != strlen( control->current->title->path ) ) ) {
		usedb=( control->root->key || !control->playstream )?TRUE:FALSE;
		/* These depend on a database */
		gtk_widget_set_visible( MP_GLDATA->widgets->button_fav, usedb );

		gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->title_current ),
								control->current->title->title );

		if( usedb ) {
			infoLine( buff, control->current->prev->title, MAXPATHLEN );
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_prev, buff );
			infoLine( buff, control->current->title, MAXPATHLEN );
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->title_current, buff );
			infoLine( buff, control->current->next->title, MAXPATHLEN );
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_next, buff );
		}
		else {
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->title_current, control->current->title->path );
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_prev, NULL );
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_next, NULL );
		}

		runner=control->current->next->next;
		buff[0]=0;
		while( ( runner != NULL ) && i < 5 ) {
			if( i > 0 ) {
				strcat( buff, "\n" );
			}
			strcat( buff, runner->title->display );
			runner=runner->next;
			i++;
		}
		if( strlen(buff) > 0 ) {
			gtk_widget_set_tooltip_text( MP_GLDATA->widgets->button_next, buff );
		}

		gtk_widget_set_sensitive( MP_GLDATA->widgets->title_current, ( control->status == mpc_play ) );
		gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->artist_current ),
							control->current->title->artist );
		gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->album_current ),
							control->current->title->album );
		gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->genre_current ),
							control->current->title->genre );

		setButtonLabel( MP_GLDATA->widgets->button_prev, control->current->prev->title->display );

		if( getDebug() > 1 ) {
			sprintf( buff, "%2i %s", control->current->title->skipcount, control->current->next->title->display );
			setButtonLabel( MP_GLDATA->widgets->button_next, buff );
		}
		else {
			setButtonLabel( MP_GLDATA->widgets->button_next, control->current->next->title->display );
		}

		gtk_widget_set_sensitive( MP_GLDATA->widgets->button_fav, ( !( control->current->title->flags & MP_FAV ) ) );

		if( 0 != control->remote ) {
			snprintf( buff, MAXPATHLEN, "%s@%s",
					(control->active < 0)?control->sname[-control->active-1]:control->profile[control->active-1],
							control->host );
			setButtonLabel( MP_GLDATA->widgets->button_profile, buff );
		}
		else if( 0 == control->active ) {
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
			snprintf( buff, MAXPATHLEN, "Playing \n%s",
					(control->active < 0)?control->sname[-control->active-1]:control->profile[control->active-1] );
			setButtonLabel( MP_GLDATA->widgets->button_profile, buff );
		}

		gtk_window_set_title ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
							   control->current->title->display );
	}

	if( control->argument == NULL ) {
		gtk_scale_button_set_value ( GTK_SCALE_BUTTON( MP_GLDATA->widgets->volume ), control->volume/100.0 );
	}
	gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->played ),
						control->playtime );
	gtk_label_set_text( GTK_LABEL( MP_GLDATA->widgets->remain ),
						control->remtime );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( MP_GLDATA->widgets->progress ),
								   control->percent/100.0 );

	g_progressLog( getMessage() );

	return 0;
}

/**
 * gtk implementation of updateUI
 * fill the widgets with current information in the control data
 *
 * needed to keep updateUI() GUI independent
 */
void g_updateHook( void *msg ) {
	mpconfig *control=getConfig();
	if( control->inUI ) {
		if( control->status == mpc_quit ) {
			gtk_main_quit();
		}
		gdk_threads_add_idle( g_updateUI, control );

		while ( gtk_events_pending () ) {
			gtk_main_iteration ();
		}
	}
}
