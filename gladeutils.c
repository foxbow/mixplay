#include "gladeutils.h"

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

G_MODULE_EXPORT void destroy( GtkWidget *widget, gpointer   data )
{
    gtk_main_quit ();
}

G_MODULE_EXPORT void db_rescan( GtkWidget *menu, gpointer   data )
{
//	struct mpcontrol_t *control;
//	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
}

G_MODULE_EXPORT void db_newscan( GtkWidget *menu, gpointer   data )
{
//	struct mpcontrol_t *control;
//	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
}

G_MODULE_EXPORT void switchProfile( GtkWidget *menu, gpointer data )
{
	struct mpcontrol_t *control;
	control=(struct mpcontrol_t *) g_object_get_qdata( (GObject *)menu, g_quark_from_static_string("control") );
	control->command=MPCMD_NXTP;
}

// THIS NEEDS TO BE MOVED ASAP!!
void popUp( int time, const char *text, ... ) {
	// set status with text...
}
