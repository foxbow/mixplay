#include "gladeutils.h"
#include <string.h>
#include <stdlib.h>

extern volatile struct mpcontrol_t *mpcontrol;

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
};

/**
 * adds a line to the popUp window and shows it
 */
static int popUp( void *data ) {
	struct mpRequestInfo_t *req;
	req=(struct mpRequestInfo_t *)data;
	int retval=0;
	char buff[1024];
	if( gtk_widget_is_visible( mpcontrol->widgets->mp_popup ) ) {
		snprintf( buff, 1024, "%s%s",
				gtk_label_get_text( GTK_LABEL( mpcontrol->widgets->popupText ) ),
				req->text );
	}
	else {
		strncpy( buff, req->text, 1024 );
	}
	gtk_label_set_text( GTK_LABEL( mpcontrol->widgets->popupText ),
						buff );
	switch( req->mode ) {
	case mpr_blocking:
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, FALSE );
		retval=gtk_dialog_run( GTK_DIALOG( mpcontrol->widgets->mp_popup ) );
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, TRUE );
		break;
	case mpr_waiting:
		gtk_widget_show_all( mpcontrol->widgets->mp_popup );
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, FALSE );
		gtk_widget_set_sensitive( mpcontrol->widgets->button_popupOkay, FALSE );
		break;
	case mpr_closing:
		gtk_widget_set_sensitive( mpcontrol->widgets->mixplay_main, TRUE );
		gtk_widget_set_sensitive( mpcontrol->widgets->button_popupOkay, TRUE );
		break;
	case mpr_normal:
		gtk_widget_show_all( mpcontrol->widgets->mp_popup );
		break;
	}
	free(req);
	return retval;
}

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *         F_WARN = print message w/o errno and return
 *         F_FAIL = print message w/o errno and exit
 *
 * @deprecated: wrapperfunction for old functions that should
 * not be used!
 */
void fail( int error, const char* msg, ... ){
	va_list args;
	char eline[512];
	struct mpRequestInfo_t *req;
	req=calloc( 1, sizeof( struct mpRequestInfo_t ) );

	va_start( args, msg );
	vsnprintf( req->text, 1024, msg, args );
	if(error > 0 ) {
		snprintf( eline, 512, "\n ERROR: %i - %s\n", abs(error), strerror( abs(error) ) );
		strncat( req->text, eline, 1024 );
		req->mode=mpr_blocking;
		mpcontrol->command=MPCMD_ERR;
	}
	else {
		req->mode=mpr_normal;
	}
	va_end(args);

	gdk_threads_add_idle( popUp, req );
	return;
}

/**
 * print the given message when the verbosity is at
 * least vl
 */
void printver( int vl, const char *msg, ... ) {
	va_list args;
	struct mpRequestInfo_t *req;
	req=calloc( 1, sizeof( struct mpRequestInfo_t ) );

	if( vl <= getVerbosity() ) {
		va_start( args, msg );
		vsnprintf( req->text, 1024, msg, args );
		va_end(args);
		req->mode=mpr_normal;
		gdk_threads_add_idle( popUp, req );
	}
}

/**
 * open a blocking requester for status/process
 * informations
 *
 * The main app and the OK button are disabled until
 * unblockReq() is called.
 */
void waitReq( const char *msg, ... ) {
	va_list args;
	struct mpRequestInfo_t *req;
	req=calloc( 1, sizeof( struct mpRequestInfo_t ) );

	va_start( args, msg );
	vsnprintf( req->text, 1024, msg, args );
	va_end(args);
	req->mode=mpr_waiting;
	gdk_threads_add_idle( popUp, req );
}

void contReq( const char *msg, ... ) {
	va_list args;
	struct mpRequestInfo_t *req;
	req=calloc( 1, sizeof( struct mpRequestInfo_t ) );

	va_start( args, msg );
	vsnprintf( req->text, 1024, msg, args );
	va_end(args);
	req->mode=mpr_closing;
	gdk_threads_add_idle( popUp, req );
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

G_MODULE_EXPORT void folder_ok_clicked_cb( GtkButton *button, gpointer data ) {

}

G_MODULE_EXPORT void folder_cancel_clicked_cb( GtkButton *button, gpointer data ) {

}

G_MODULE_EXPORT void destroy( GtkWidget *widget, gpointer   data )
{
    gtk_main_quit ();
}

G_MODULE_EXPORT void db_clean( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=MPCMD_DBCLEAN;
	waitReq( "Clean database" );
}

G_MODULE_EXPORT void db_scan( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=MPCMD_DBSCAN;
	waitReq( "Add new titles" );
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data )
{
	mpcontrol->command=MPCMD_NXTP;
}

G_MODULE_EXPORT void popupCancel_clicked( GtkButton *button, gpointer data ) {
	gtk_widget_hide( mpcontrol->widgets->mp_popup );
	if( MPCMD_ERR == mpcontrol->command ) {
		mpcontrol->command=MPCMD_QUIT;
		gtk_main_quit ();
	}
}

G_MODULE_EXPORT void popupOkay_clicked( GtkButton *button, gpointer data ) {
	gtk_label_set_text( GTK_LABEL( mpcontrol->widgets->popupText ),
					"" );
	gtk_widget_hide( mpcontrol->widgets->mp_popup );
	if( MPCMD_ERR == mpcontrol->command ) {
		mpcontrol->command=MPCMD_QUIT;
		gtk_main_quit ();
	}
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
