#include "player.h"
#include "gladeutils.h"
#include <string.h>
#include <stdlib.h>

pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;

extern struct mpcontrol_t *mpcontrol;

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
 * kind of threadsafe...
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
//	if( strlen( mpcontrol->log ) == 0 ){
//		pthread_mutex_lock( &msglock );
		gtk_widget_destroy( GTK_WIDGET( dialog ) );
		mpcontrol->widgets->mp_popup=NULL;
		mpcontrol->log[0]='\0';
//		pthread_mutex_unlock( &msglock );
//	}
}

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
	line=calloc( 512, sizeof( char ) );

	pthread_mutex_lock( &msglock );
	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end(args);
	gdk_threads_add_idle( g_progressLog, line );
	pthread_mutex_unlock( &msglock );
	while (gtk_events_pending ()) gtk_main_iteration ();

}

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

static void setButtonLabel( GtkWidget *button, const char *text ) {
	char *label;
	label=calloc( 60, sizeof( char ) );
	strip( label, text, 60 );
	gtk_button_set_label( GTK_BUTTON( button ), label );
	free(label);
}


int updateUI( void *data ) {
	struct	mpcontrol_t *control;
	char	buff[MAXPATHLEN];
	gboolean	usedb;
	control=(struct mpcontrol_t*)data;

	usedb=(0 == strlen( control->dbname ) )?FALSE:TRUE;

	if( mpc_quit == control->command ) {
		printver(2, "Already closing..\n");
		return 0;
	}

	if( control->widgets->title_current == NULL ) {
		printver(2, "No title yet..\n");
		return 0;
	}

	// These depend on a database
	gtk_widget_set_visible( control->widgets->button_fav, usedb );
	gtk_widget_set_visible( control->widgets->button_dnp, usedb );
	gtk_widget_set_visible( control->widgets->button_replay, usedb );
	gtk_widget_set_visible( control->widgets->button_database, usedb );

	// these don't make sense when a stream is played
	gtk_widget_set_visible( control->widgets->button_next, !(control->playstream) );
	gtk_widget_set_sensitive( control->widgets->button_prev, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->progress, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->played, !(control->playstream) );
	gtk_widget_set_visible( control->widgets->remain, !(control->playstream) );

	if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
		gtk_label_set_text( GTK_LABEL( control->widgets->title_current ),
				control->current->title );
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

//		gtk_window_set_title ( GTK_WINDOW( control->widgets->mixplay_main ),
//							  control->current->display );
	}


	if( (control->current != NULL ) && ( mpcontrol->debug > 1) ) {
		snprintf( buff, MAXPATHLEN, "[%i] %s", control->current->played,
				( mpc_play == control->status )?"pause":"play" );
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ),
				buff );
	}
	else {
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ),
				( mpc_play == control->status )?"pause":"play" );
	}

	gtk_label_set_text( GTK_LABEL( control->widgets->played ),
			control->playtime );
	gtk_label_set_text( GTK_LABEL( control->widgets->remain ),
			control->remtime );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( control->widgets->progress),
			control->percent/100.0 );

	return 0;
}
