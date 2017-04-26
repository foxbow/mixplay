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
#ifdef DEBUG
	printf( "FAIL: %s\n", line );
#endif

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

		pthread_mutex_unlock( &msglock );

#ifdef DEBUG
		printf("VER: %s", line );
#endif
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
#ifdef DEBUG
	printf("LOG: %s\n", line);
#endif

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
#ifdef DEBUG
	printf("DONE\n");
#endif
	if( NULL == mpcontrol->widgets->mp_popup ){
		fail( F_FAIL, "No progress request open!" );
	}
	strncat( mpcontrol->log, "\n", 1024 );
	strncat( mpcontrol->log, "Done.", 1024 );
	gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
			"%s", mpcontrol->log );
	mpcontrol->log[0]='\0';
	while (gtk_events_pending ()) gtk_main_iteration ();
}
