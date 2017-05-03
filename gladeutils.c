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

	if( F_WARN == error ) {
		type=GTK_MESSAGE_WARNING;
	}

	va_start( args, msg );
	vsnprintf( line, 1024, msg, args );
	va_end(args);
	if( mpcontrol->debug ) printf( "FAIL: %s\n", line );

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
		exit(-1);
	}

	return;
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

	if( vl <= getVerbosity() ) {
		pthread_mutex_lock( &msglock );
		if( vl < 2 ) {
			va_start( args, msg );
			vsnprintf( line, 512, msg, args );
			va_end(args);

			if( NULL == mpcontrol->widgets->mp_popup ) {
				fail(F_FAIL, "No log widget open!");
			}

			strncat( mpcontrol->log, line, 1024 );
			gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
					"%s", mpcontrol->log );

			gtk_widget_queue_draw( mpcontrol->widgets->mp_popup );
			while (gtk_events_pending ()) gtk_main_iteration ();
			if( mpcontrol->debug ) {
				fprintf( stderr, "%i: %s", vl, line );
			}
		}
		else {
			vfprintf( stderr, msg, args );
		}
		pthread_mutex_unlock( &msglock );
	}

}

/**
 * callback for the progress close button
 * only closes the requester if the message buffer
 * is empty.
 */
static void progressClose( GtkDialog *dialog, gint res, gpointer data )
{
	if( strlen( mpcontrol->log ) == 0 ){
		gtk_widget_destroy( GTK_WIDGET( dialog ) );
		mpcontrol->widgets->mp_popup=NULL;
	}
}

/**
 * Opens an info requester
 */
void progressLog( const char *msg, ... ) {
	va_list args;
	char line[512];

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end(args);

	if( mpcontrol->debug ) printf("LOG: %s\n", line);

	if( NULL != mpcontrol->widgets->mp_popup ) {
		fail( F_WARN, "Log widget is already open!" );
		return;
	}
	mpcontrol->widgets->mp_popup = gtk_message_dialog_new (GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
			"%s", line );
	gtk_dialog_add_button( GTK_DIALOG( mpcontrol->widgets->mp_popup ), "OK", 1 );
	g_signal_connect_swapped (mpcontrol->widgets->mp_popup, "response",
	                          G_CALLBACK( progressClose ),
	                          mpcontrol->widgets->mp_popup );
	gtk_widget_show_all( mpcontrol->widgets->mp_popup );
}

/**
 * enables closing of the info requester
 */
void progressDone() {
	if( mpcontrol->debug ) printf("DONE\n");
	if( NULL == mpcontrol->widgets->mp_popup ){
		fail( F_FAIL, "No progress request open!" );
	}
//	strncat( mpcontrol->log, "\n", 1024 );
	strncat( mpcontrol->log, "Done.", 1024 );
	gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
			"%s", mpcontrol->log );
	mpcontrol->log[0]='\0';
	while (gtk_events_pending ()) gtk_main_iteration ();
}

int updateUI( void *data ) {
	struct mpcontrol_t *control;
	char buff[MAXPATHLEN];
	control=(struct mpcontrol_t*)data;

	if( ( NULL != control->current ) && ( 0 != strlen( control->current->path ) ) ) {
		gtk_label_set_text( GTK_LABEL( control->widgets->title_current ),
				control->current->title );
		gtk_label_set_text( GTK_LABEL( control->widgets->artist_current ),
				control->current->artist );
		gtk_label_set_text( GTK_LABEL( control->widgets->album_current ),
				control->current->album );
		gtk_label_set_text( GTK_LABEL( control->widgets->genre_current ),
				control->current->genre );
/*
		gtk_label_set_text( GTK_LABEL( control->widgets->displayname_prev ),
				control->current->plprev->display );
		gtk_label_set_text( GTK_LABEL( control->widgets->displayname_next ),
				control->current->plnext->display );
*/
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_prev ),
				control->current->plprev->display );
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
				control->current->plnext->display );

		if( mpcontrol->debug ) {
			snprintf( buff, MAXPATHLEN, "[%i]", control->current->played );
			gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ),
					buff );
			sprintf( buff, "%2i", control->current->skipped );
			gtk_button_set_label( GTK_BUTTON( control->widgets->button_next ),
					buff );
		}
/*
		if( control->current->skipped > 2 ) {
			gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
					control->widgets->noentry );
		}
		else {
			gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
					control->widgets->down );
		}
*/
		gtk_widget_set_sensitive( control->widgets->button_fav, ( !(control->current->flags & MP_FAV) ) );

		gtk_window_set_title ( GTK_WINDOW( control->widgets->mixplay_main ),
							  control->current->display );
	}

	if( mpc_play == control->status ) {
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ), "pause" );
	}
	else {
		gtk_button_set_label( GTK_BUTTON( control->widgets->button_play ), "play" );
	}


/** skipcontrol is off
	if( control->percent < 5 ) {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->skip );
	}
	else {
		gtk_button_set_image( GTK_BUTTON( control->widgets->button_next ),
				control->widgets->down );
	}
**/
	// other settings
	gtk_label_set_text( GTK_LABEL( control->widgets->played ),
			control->playtime );
	gtk_label_set_text( GTK_LABEL( control->widgets->remain ),
			control->remtime );
	gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( control->widgets->progress),
			control->percent/100.0 );

	return 0;
}
