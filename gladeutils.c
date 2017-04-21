#include "gladeutils.h"
#include <string.h>
#include <stdlib.h>

pthread_mutex_t msglock=PTHREAD_MUTEX_INITIALIZER;

extern struct mpcontrol_t *mpcontrol;

enum mpRequestmode {
	mpr_normal,		// async mode, shows info and can be closed at will
	mpr_blocking,   // can be closed anytime, blocks operation
	mpr_waiting,	// blocks and cannot be closed
	mpr_closing		// finishes waiting
};

struct mpRequestInfo_t {
	char	title[80];
	char	text[1024];
	enum mpRequestmode	mode;
	int clean;
};

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
		mpcontrol->command=MPCMD_QUIT;
		gtk_main_quit();
		exit(-1);
	}

	return;
}

/**
 * print the given message when the verbosity is at
 * least vl
 *
 * threadsafe...
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
			progressLog("Info:");
			g_signal_connect_swapped (mpcontrol->widgets->mp_popup, "response",
			                          G_CALLBACK (gtk_widget_destroy),
			                          mpcontrol->widgets->mp_popup );
			gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, TRUE );
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
 * disables the main app window and opens an info requester
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

	if( NULL == mpcontrol->widgets->mp_popup ) {
		mpcontrol->log[0]='\0';
		mpcontrol->widgets->mp_popup = gtk_message_dialog_new (GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				"%s", line );
		gtk_widget_show_all( mpcontrol->widgets->mp_popup );
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, FALSE );
	}
	else {
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, FALSE );
//		fail( F_FAIL, "progress req already open!\n%s", line );
	}

}
/**
 * enables close button of info request and reenables main window
 */
void progressDone( const char *msg, ... ) {
	va_list args;
	char line[512];

	va_start( args, msg );
	vsnprintf( line, 512, msg, args );
	va_end(args);
	printf("DONE: %s\n", line);

	if( NULL == mpcontrol->widgets->mp_popup ) {
		fail( F_FAIL, "No progress request open for\n%s", line );
	}

	strncat( mpcontrol->log, "\n", 1024 );
	strncat( mpcontrol->log, line, 1024 );
	gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( mpcontrol->widgets->mp_popup ),
			"%s", mpcontrol->log );

	g_signal_connect_swapped (mpcontrol->widgets->mp_popup, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          mpcontrol->widgets->mp_popup );
	gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, TRUE );
}

G_MODULE_EXPORT void markfav( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_MFAV;
    /* CODE HERE */
}

G_MODULE_EXPORT void markdnp( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_MDNP;
    /* CODE HERE */
}

G_MODULE_EXPORT void playpause( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_PLAY;
    /* CODE HERE */
}

G_MODULE_EXPORT void playprev( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_PREV;
    /* CODE HERE */
}

G_MODULE_EXPORT void playnext( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_NEXT;
	/* CODE HERE */
}

G_MODULE_EXPORT void replay( GtkButton *button, gpointer data ) {
	mpcontrol->command=MPCMD_REPL;
	/* CODE HERE */
}

G_MODULE_EXPORT void destroy( GtkWidget *widget, gpointer   data )
{
    gtk_main_quit ();
}

G_MODULE_EXPORT void db_clean( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=MPCMD_DBCLEAN;
	progressLog( "Clean database" );
}

G_MODULE_EXPORT void db_scan( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=MPCMD_DBSCAN;
	progressLog( "Add new titles" );
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data )
{
	mpcontrol->command=MPCMD_NXTP;
}

G_MODULE_EXPORT void info_db( GtkWidget *menu, gpointer data ) {
	fail( 0, "basedir: %s\ndnplist: %s\nfavlist: %s",
			mpcontrol->musicdir, mpcontrol->dnpname, mpcontrol->favname );

}

G_MODULE_EXPORT void info_title( GtkWidget *menu, gpointer data ) {
	if( 0 != mpcontrol->current->key ) {
		fail( 0, "%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s",
				mpcontrol->current->path, mpcontrol->current->genre,
				mpcontrol->current->key, mpcontrol->current->played,
				mpcontrol->current->skipped,
				ONOFF(~(mpcontrol->current->flags)&MP_CNTD),
				ONOFF(~(mpcontrol->current->flags)&MP_SKPD));
	}
	else {
		fail( 0, mpcontrol->current->path );
	}
}
