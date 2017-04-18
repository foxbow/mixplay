#include "gladeutils.h"
#include <string.h>
#include <stdlib.h>

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *         F_WARN = print message w/o errno and return
 *         F_FAIL = print message w/o errno and exit
 */
void fail( int error, const char* msg, ... ){
	va_list args;
	va_start( args, msg );
	char eline[512];
	char line[1024];

	snprintf( line, 1024, msg, args );
	if(error > 0 ) {
		snprintf( eline, 512, "\n ERROR: %i - %s\n", abs(error), strerror( abs(error) ) );
		strncat( line, eline, 1024 );
	}
	va_end(args);

	puts(line); // @todo - request!
	return;
}

G_MODULE_EXPORT void markfav( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_MFAV;
    /* CODE HERE */
}

G_MODULE_EXPORT void markdnp( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_MDNP;

    /* CODE HERE */
}

G_MODULE_EXPORT void playpause( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_PLAY;

    /* CODE HERE */
}

G_MODULE_EXPORT void playprev( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_PREV;

    /* CODE HERE */
}

G_MODULE_EXPORT void playnext( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_NEXT;
	/* CODE HERE */
}

G_MODULE_EXPORT void replay( GtkButton *button, gpointer data ) {
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)button, g_quark_from_static_string("control") );
	control->command=MPCMD_REPL;
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

G_MODULE_EXPORT void db_rescan( GtkWidget *menu, gpointer   data )
{
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
	control->command=MPCMD_DBSCAN;
}

G_MODULE_EXPORT void db_newscan( GtkWidget *menu, gpointer   data )
{
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
	control->command=MPCMD_DBNEW;
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data )
{
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
	gtk_menu_item_get_label(GTK_MENU_ITEM( menu ) );
	control->command=MPCMD_NXTP;
}

// THIS NEEDS TO BE MOVED ASAP!!
void popUp( int time, const char *text, ... ) {
	// set status with text...
}
