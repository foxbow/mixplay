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
		gtk_window_set_title( GTK_WINDOW( mpcontrol->widgets->mp_popup ), (char *)text );
		gtk_widget_queue_draw( mpcontrol->widgets->mp_popup );
	}
	else if( mpcontrol->widgets->album_current != NULL ) {
		gtk_label_set_text( GTK_LABEL( mpcontrol->widgets->album_current ),
				text );
		gtk_widget_queue_draw( mpcontrol->widgets->mixplay_main );
	}
	free(text);
	return 0;
}

/**
 * activity indication
 */
static unsigned int _ftrpos=0;
void activity( const char *msg, ... ){
	char roller[5]="|/-\\";
	char text[256]="";
	char *line;
	int pos;

	if( (_ftrpos%100) == 0 ) {
		line=falloc( NAMELEN, sizeof(char) );
		pos=(_ftrpos/100)%4;

		va_list args;
		va_start( args, msg );
		vsprintf( text, msg, args );
		va_end( args );
		snprintf( line, NAMELEN, "%s %c", text, roller[pos] );
		gdk_threads_add_idle( g_activity, line );
	}
	_ftrpos=(_ftrpos+1)%400;
}

/*
 * Show errormessage quit
 * msg - Message to print
 * error - errno that was set or 0 for no error to print
 */
void fail( int error, const char* msg, ... ){
	va_list args;
	char line[1024];

	GtkWidget *dialog;
	GtkMessageType type = GTK_MESSAGE_ERROR;

	// fail calls are considered mutex and should never ever stack!
	pthread_mutex_lock( &msglock );

	va_start( args, msg );
	vsnprintf( line, 1024, msg, args );
	va_end(args);

	if( F_WARN == error ) {
		type=GTK_MESSAGE_WARNING;
		fprintf( stderr, "WARN: %s\n", line );
	}
	else {
		fprintf( stderr, "FAIL: %s\n", line );
	}

	if(error > 0 ) {
		dialog = gtk_message_dialog_new (GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE,
				"%s\nERROR: %i - %s", line, abs(error), strerror( abs(error) ));
	}
	else {
		dialog = gtk_message_dialog_new (GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE,
				"%s", line );
	}
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if( error != F_WARN ) {
		mpcontrol->command=mpc_quit;
		gtk_main_quit();
	}
	pthread_mutex_unlock( &msglock );

	return;
}

/**
 * actual gtk code for progressAdd
 * is added as an idle thread to the main thread
 */
static int g_progressAdd( void *line ) {
	gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
			"%s", (char *)line );
	gtk_widget_queue_draw( mpcontrol->widgets->mp_popup );
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
	char line[512];

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end(args);

	if( ( mpcontrol->debug || mpcontrol->widgets->mp_popup ) && ( vl < 2 ) ) {
		pthread_mutex_lock( &msglock );
		strncat( mpcontrol->log, line, 2047-strlen( mpcontrol->log) );
		gdk_threads_add_idle( g_progressAdd, mpcontrol->log );
		while (gtk_events_pending ()) gtk_main_iteration ();
		pthread_mutex_unlock( &msglock );
	}

	if( ( vl > 0 ) && ( vl <= getVerbosity() ) ) {
		fprintf( stderr, "%i: %s", vl, line );
	}
}

/**
 * callback for the progress close button
 * only closes the requester if the message buffer
 * is empty.
 */
static void progressClose( GtkDialog *dialog, gint res, gpointer data )
{
	// make sure that nothing is added while we close the dialog
	pthread_mutex_lock( &msglock );
	gtk_widget_destroy( GTK_WIDGET( dialog ) );
	mpcontrol->widgets->mp_popup=NULL;
	mpcontrol->log[0]='\0';
	pthread_mutex_unlock( &msglock );
}

/**
 * actual gtk code for prgressLog
 * is added as an idle thread to the main thread
 */
static int g_progressLog( void *line ) {
	if( !mpcontrol->debug && ( getVerbosity() > 0 ) ) printf("LOG: %s\n", (char *)line);

	if( NULL != mpcontrol->widgets->mp_popup ) {
		g_progressAdd( line );
	}
	else {
		mpcontrol->widgets->mp_popup = gtk_message_dialog_new (GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
				"%s", (char *)line );
		gtk_dialog_add_button( GTK_DIALOG( mpcontrol->widgets->mp_popup ), "OK", 1 );
		g_signal_connect_swapped (mpcontrol->widgets->mp_popup, "response",
								  G_CALLBACK( progressClose ),
								  mpcontrol->widgets->mp_popup );
		gtk_widget_set_sensitive( mpcontrol->widgets->mp_popup, FALSE );
		gtk_widget_show_all( mpcontrol->widgets->mp_popup );
	}
	free( line );
	return 0;
}

/**
 * Opens an info requester
 */
void progressLog( const char *msg, ... ) {
	va_list args;
	char *line;
	line=falloc( 512, sizeof( char ) );

	pthread_mutex_lock( &msglock );
	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end(args);
	gdk_threads_add_idle( g_progressLog, line );
	pthread_mutex_unlock( &msglock );
	while (gtk_events_pending ()) gtk_main_iteration ();

}

/**
 * actual gtk code for prgressDone
 * is added as an idle thread to the main thread
 */
static int g_progressDone( void *line ) {
	gtk_widget_set_sensitive( mpcontrol->widgets->mp_popup, TRUE );
	return g_progressAdd( line );
}

/**
 * enables closing of the info requester
 */
void progressDone() {
	if( !mpcontrol->debug && ( getVerbosity() > 0 ) ) printf("DONE\n");
	if( NULL == mpcontrol->widgets->mp_popup ){
		fail( F_FAIL, "No progress request open!" );
	}
	pthread_mutex_lock( &msglock );
	strncat( mpcontrol->log, "DONE\n", 2047-strlen( mpcontrol->log) );
	gdk_threads_add_idle( g_progressDone, mpcontrol->log );
	pthread_mutex_unlock( &msglock );
	while (gtk_events_pending ()) gtk_main_iteration ();
}

/**
 * wrapper to cut off text before setting the label
 */
static void setButtonLabel( GtkWidget *button, const char *text ) {
	char *label;
	label=falloc( NAMELEN, sizeof( char ) );
	strip( label, text, NAMELEN );
	gtk_button_set_label( GTK_BUTTON( button ), label );
	free(label);
}

/**
 * fill the widgets with current information in the control data
 *
 * This function is supposed to be called with gtk_add_thread_idle() to make update
 * work in multithreaded environments.
 */
int updateUI( void *data ) {
	struct	mpcontrol_t *control;
	char	buff[MAXPATHLEN];
	gboolean	usedb;
	control=(struct mpcontrol_t*)data;

	if( mpc_quit == control->command ) {
		printver(2, "Already closing..\n");
		return 0;
	}

	if( control->widgets->title_current == NULL ) {
		printver(2, "No title widget yet..\n");
		return 0;
	}

	// these don't make sense when a stream is played
	gtk_widget_set_visible( control->widgets->button_next, !(control->playstream) );
	gtk_widget_set_sensitive( control->widgets->button_prev, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->progress, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->played, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->remain, !(control->playstream) );

	if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
		usedb=( control->root->key )?TRUE:FALSE;
		// These depend on a database
		gtk_widget_set_visible( control->widgets->button_fav, usedb );
		gtk_widget_set_visible( control->widgets->button_dnp, usedb );
		gtk_widget_set_visible( control->widgets->button_replay, usedb );
		gtk_widget_set_visible( control->widgets->button_database, usedb );

		gtk_label_set_text( GTK_LABEL( control->widgets->title_current ),
				control->current->title );
		gtk_widget_set_sensitive( control->widgets->title_current, ( control->status == mpc_play ) );
		gtk_label_set_text( GTK_LABEL( control->widgets->artist_current ),
				control->current->artist );
		gtk_label_set_text( GTK_LABEL( control->widgets->album_current ),
				control->current->album );
		gtk_label_set_text( GTK_LABEL( control->widgets->genre_current ),
				control->current->genre );
		setButtonLabel( control->widgets->button_prev, control->current->plprev->display );
		if( mpcontrol->debug > 1 ) {
			sprintf( buff, "%2i %s", control->current->skipped, control->current->plnext->display );
			setButtonLabel( control->widgets->button_next, buff );
		}
		else {
			setButtonLabel( control->widgets->button_next, control->current->plnext->display );
		}
		gtk_widget_set_sensitive( control->widgets->button_fav, ( !(control->current->flags & MP_FAV) ) );

		gtk_window_set_title ( GTK_WINDOW( control->widgets->mixplay_main ),
							  control->current->display );
	}


	gtk_label_set_text( GTK_LABEL( control->widgets->played ),
			control->playtime );
	gtk_label_set_text( GTK_LABEL( control->widgets->remain ),
			control->remtime );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( control->widgets->progress),
			control->percent/100.0 );

	return 0;
}
