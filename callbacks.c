/*
 * callbacks.c
 *
 *  Created on: 25.04.2017
 *      Author: bweber
 */
#include "player.h"
#include "gladeutils.h"
extern struct mpcontrol_t *mpcontrol;


/**
 * called by the info->info menu
 * standard info requester
 */
G_MODULE_EXPORT void showInfo( GtkButton *button, gpointer data ) {
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
}

/**
 * called by the FAV button
 * asks if current title, artist or album should be marked favourite
 * sets the mpcontrol command accordingly
 */
G_MODULE_EXPORT void markfav( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
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
}

/**
 * called by the DNP button
 * asks if current title, artist or album should be marked as DNP
 * sets the mpcontrol command accordingly
 */
G_MODULE_EXPORT void markdnp( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	dialog = gtk_message_dialog_new(
			GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
			GTK_DIALOG_DESTROY_WITH_PARENT,
	        GTK_MESSAGE_QUESTION,
	        GTK_BUTTONS_NONE,
	        "Do not play\n%s\nAlbum: %s",
	        mpcontrol->current->display,
	        mpcontrol->current->album );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
            "T_itle",  mpc_dnptitle,
            "A_lbum",  mpc_dnpalbum,
            "_Artist", mpc_dnpartist,
            "_Cancel", GTK_RESPONSE_CANCEL,
            NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );
	if( reply > 0 ) {
		setCommand( mpcontrol, reply );
	}
}

/**
 * called by the play/pause button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playpause( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_play );
    /* CODE HERE */
}

/**
 * called by the up button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playprev( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_prev );
    /* CODE HERE */
}

/**
 * called by the down button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void playnext( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_next );
	/* CODE HERE */
}

/**
 * called by the repl button
 * just sets the mpcontrol command
 */
G_MODULE_EXPORT void replay( GtkButton *button, gpointer data ) {
	setCommand( mpcontrol, mpc_repl );
	/* CODE HERE */
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
 * called by the database->clean menu
 * initializes a check run on the DB
 */
G_MODULE_EXPORT void db_clean( GtkWidget *menu, gpointer   data ) {
	setCommand( mpcontrol, mpc_dbclean);
}

G_MODULE_EXPORT void db_scan( GtkWidget *menu, gpointer   data ) {
	setCommand( mpcontrol, mpc_dbscan );
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data ) {
	setCommand( mpcontrol, mpc_profile );
}

G_MODULE_EXPORT void info_db( GtkWidget *menu, gpointer data ) {
	progressLog( "Database Info" );
	progress( "Music dir: %s\n", mpcontrol->musicdir );
	dumpInfo( mpcontrol->root, -1 );
	progressDone();
}

G_MODULE_EXPORT void info_title( GtkWidget *menu, gpointer data ) {
	progressLog( "Title Info" );
	if( 0 != mpcontrol->current->key ) {
		progress( "%s\n", mpcontrol->current->path );
		progress( "Genre: %s\n", mpcontrol->current->genre );
		progress( "Key: %04i\n", mpcontrol->current->key );
		progress( "playcount: %i\n", mpcontrol->current->played );
		progress( "skipcount: %i\n", mpcontrol->current->skipped );
		progress( "Count: %s - Skip: %s\n", ONOFF(~(mpcontrol->current->flags)&MP_CNTD),
				ONOFF(~(mpcontrol->current->flags)&MP_SKPD) );
	}
	else {
		progress( mpcontrol->current->path );
	}
	progressDone();
}

/**
 * settings/info route
 */
G_MODULE_EXPORT void infoStart( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	if( 0 != mpcontrol->current->key ) {
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
		progress( "Music dir: %s\n", mpcontrol->musicdir );
		dumpInfo( mpcontrol->root, -1 );
		progressDone();
	break;
	case 2:
		setCommand( mpcontrol, mpc_dbclean);
	break;
	}
}

G_MODULE_EXPORT void profileStart( GtkButton *button, gpointer data ) {
	fail( F_WARN, "Not yet supported" );
}