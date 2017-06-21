/*
 * callbacks.c
 *
 *  Created on: 25.04.2017
 *      Author: bweber
 */
#include "player.h"
#include "gladeutils.h"
#include <string.h>

extern struct mpcontrol_t *mpcontrol;

/**
 * called by the FAV button
 * asks if current title, artist or album should be marked favourite
 * sets the mpcontrol command accordingly
 */
G_MODULE_EXPORT void markfav( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
/*
 * Do not pause. This may mess up things, if the current title
 * changes while the requester is still open. Then the next title
 * will be marked. This is not ideal but better than having a pause
 * during play.
 * @todo: make sure the correct title is marked.
 *
	if( mpcontrol->status == mpc_play ) {
		setCommand( mpcontrol, mpc_play );
	}
 */
	dialog = gtk_message_dialog_new(
			GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT,
	        GTK_MESSAGE_QUESTION,
	        GTK_BUTTONS_NONE,
	        "Mark as favourite\n%s\nAlbum: %s",
	        mpcontrol->current->display,
	        mpcontrol->current->album );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "T_itle",  mpc_favtitle,
            "A_lbum",  mpc_favalbum,
            "_Artist", mpc_favartist,
            "_Cancel", GTK_RESPONSE_CANCEL,
            NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	if( reply > 0 ) {
		setCommand( mpcontrol, reply );
	}
//	setCommand( mpcontrol, mpc_play );
}

/**
 * called by the DNP button
 * asks if current title, artist or album should be marked as DNP
 * sets the mpcontrol command accordingly
 */
G_MODULE_EXPORT void markdnp( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	if( mpcontrol->status == mpc_play ) {
		setCommand( mpcontrol, mpc_play );
	}
	dialog = gtk_message_dialog_new(
			GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT,
	        GTK_MESSAGE_QUESTION,
	        GTK_BUTTONS_NONE,
	        "Do not play\n%s\nAlbum: %s\nGenre: %s",
	        mpcontrol->current->display,
	        mpcontrol->current->album,
	        mpcontrol->current->genre );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "T_itle",  mpc_dnptitle,
            "A_lbum",  mpc_dnpalbum,
            "_Artist", mpc_dnpartist,
            "_Genre", mpc_dnpgenre,
            "_Cancel", GTK_RESPONSE_CANCEL,
            NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	if( reply > 0 ) {
		setCommand( mpcontrol, reply );
	}
	if( mpcontrol->status == mpc_idle ) {
		setCommand( mpcontrol, mpc_play );
	}
}

// @TODO: consider pulling the 'simple' button callbacks into one

/**
 * called by the play/pause button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playPause( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_play );
}

/**
 * called by the up button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playprev( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_prev );
}

/**
 * called by the down button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playnext( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_next );
}

/**
 * called by the repl button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void replay( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_repl );
}

/**
 * called by the window->close button
 * called by the info->quit menu
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void destroy( GtkWidget *widget, gpointer   data ) {
	setCommand( mpcontrol, mpc_quit );
    gtk_main_quit ();
}

/**
 * settings/info route
 */
G_MODULE_EXPORT void infoStart( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	if( mpcontrol->root->key != 0  ) {
		dialog = gtk_message_dialog_new(
				GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_INFO,
				GTK_BUTTONS_NONE,
				"%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s\n",
				mpcontrol->current->path , mpcontrol->current->genre,
				mpcontrol->current->key, mpcontrol->current->played,
				mpcontrol->current->skipped, ONOFF(~(mpcontrol->current->flags)&MP_CNTD),
				ONOFF(~(mpcontrol->current->flags)&MP_SKPD));
	}
	else {
		dialog = gtk_message_dialog_new(
				GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				GTK_DIALOG_DESTROY_WITH_PARENT,
				GTK_MESSAGE_INFO,
				GTK_BUTTONS_NONE,
				"%s", mpcontrol->current->path );
	}

	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "App Info",  1,
            "Quit",  2,
            "OK", GTK_RESPONSE_OK,
            NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	switch( reply ) {
	case 1:
		gtk_show_about_dialog ( GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
				"program-name", "gmixplay",
				"copyright", "2017 B.Weber",
				"license-type", GTK_LICENSE_MIT_X11,
				"version", VERSION,
				"comments", "GTK based front-end to mpg123, planned to replace my old "
					"squeezebox/squeezeboxserver and act as a radio replacement to play "
					"background music but stay sleek enough to run on a mini ARM board.",
				"website", "https://github.com/foxbow/mixplay",
		        NULL, NULL);
		break;
	case 2:
		setCommand( mpcontrol, mpc_quit );
	    gtk_main_quit ();
		break;
	}
}

/**
 * invoked by the 'database' button
 */
G_MODULE_EXPORT void databaseStart( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	dialog = gtk_message_dialog_new(
			GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT,
	        GTK_MESSAGE_INFO,
	        GTK_BUTTONS_NONE,
	        "Database" );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "Info", 1,
            "Cleanup", 2,
            "Okay", GTK_RESPONSE_OK,
            NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	switch( reply ) {
	case 1:
		progressLog( "Database Info" );
		progressAdd( "Music dir: %s\n", mpcontrol->musicdir );
		dumpInfo( mpcontrol->root, -1 );
		progressDone( "End Database info.");
	break;
	case 2:
		setCommand( mpcontrol, mpc_dbclean);
	break;
	}
}

/**
 * invoked by the 'profile' button
 */
G_MODULE_EXPORT void profileStart( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
//	GtkWidget *msgArea;

	int reply;
	dialog = gtk_message_dialog_new(
			GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT,
	        GTK_MESSAGE_INFO,
	        GTK_BUTTONS_NONE,
	        "Profiles/Channels" );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "Okay", GTK_RESPONSE_OK,
            "Cancel", GTK_RESPONSE_CANCEL,
            NULL );

//	msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );


	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	switch( reply ) {
	case GTK_RESPONSE_OK:
		fail( F_WARN, "%s\nNot yet supported", mpcontrol->profile[ mpcontrol->active ] );
	break;
	}

}
