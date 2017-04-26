/*
 * callbacks.c
 *
 *  Created on: 25.04.2017
 *      Author: bweber
 */
#include "gladeutils.h"
extern struct mpcontrol_t *mpcontrol;

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
		mpcontrol->command=reply;
	}
}

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
		mpcontrol->command=reply;
	}
}

G_MODULE_EXPORT void playpause( GtkButton *button, gpointer data ) {
	mpcontrol->command=mpc_play;
    /* CODE HERE */
}

G_MODULE_EXPORT void playprev( GtkButton *button, gpointer data ) {
	mpcontrol->command=mpc_prev;
    /* CODE HERE */
}

G_MODULE_EXPORT void playnext( GtkButton *button, gpointer data ) {
	mpcontrol->command=mpc_next;
	/* CODE HERE */
}

G_MODULE_EXPORT void replay( GtkButton *button, gpointer data ) {
	mpcontrol->command=mpc_repl;
	/* CODE HERE */
}

G_MODULE_EXPORT void destroy( GtkWidget *widget, gpointer   data )
{
	mpcontrol->command=mpc_quit;
    gtk_main_quit ();
}

G_MODULE_EXPORT void db_clean( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=mpc_dbclean;
	progressLog( "Clean database" );
}

G_MODULE_EXPORT void db_scan( GtkWidget *menu, gpointer   data )
{
	mpcontrol->command=mpc_dbscan;
	progressLog( "Add new titles" );
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data )
{
	mpcontrol->command=mpc_profile;
}

G_MODULE_EXPORT void info_db( GtkWidget *menu, gpointer data ) {
	progressLog( "Database Info" );
	progress( "Music dir: ", mpcontrol->musicdir );
//	printver( 0, "basedir: %s\ndnplist: %s\nfavlist: %s\n",
//			mpcontrol->musicdir, mpcontrol->dnpname, mpcontrol->favname );
	dumpInfo( mpcontrol->root, -1 );
	progressDone();
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
