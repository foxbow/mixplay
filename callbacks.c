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
 * called by the up button
 * just sets the mpcontrol command
 */
void playprev( GtkButton *button, gpointer data ) {
    setCommand( mpcontrol, mpc_prev );
}

/**
 * called by the down button
 * just sets the mpcontrol command
 */
void playnext( GtkButton *button, gpointer data ) {
    setCommand( mpcontrol, mpc_next );
}

/**
 * called by the FAV button
 * asks if current title, artist or album should be marked favourite
 * sets the mpcontrol command accordingly
 */
void markfav( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    struct entry_t *title = mpcontrol->current;
    int reply;
    /*
     * Do not pause. This may mess up things, if the current title
     * changes while the requester is still open. Then the next title
     * will be marked. This is not ideal but better than having a pause
     * during play.
     */
    dialog = gtk_message_dialog_new(
                 GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_QUESTION,
                 GTK_BUTTONS_NONE,
                 "Mark as favourite\n%s\nAlbum: %s",
                 title->display,
                 title->album );
    gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                            "_Title",  mpc_favtitle,
                            "A_lbum",  mpc_favalbum,
                            "_Artist", mpc_favartist,
                            "_Cancel", GTK_RESPONSE_CANCEL,
                            NULL );
    reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
    gtk_widget_destroy( dialog );

    switch( reply ) {
    case mpc_favtitle:
        addToFile( mpcontrol->favname, title->display, "d=" );
        title->flags|=MP_FAV;
        break;

    case mpc_favalbum:
        addToFile( mpcontrol->favname, title->album, "l=" );
        markFavourite( title, SL_ALBUM );
        break;

    case mpc_favartist:
        addToFile( mpcontrol->favname, title->artist, "a=" );
        markFavourite( title, SL_ARTIST );
        break;
    }
}

/**
 * called by the play/pause button
 * just sets the mpcontrol command
 */
void playPause( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    int reply;

    if( mpcontrol->status == mpc_play ) {
        setCommand( mpcontrol, mpc_play );

        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Pause" );
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                                "Play",  mpc_play,
                                "Replay",  mpc_repl,
                                "DNP",  mpc_dnptitle,
								"Quit", mpc_quit,
                                NULL );
        reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
        gtk_widget_destroy( dialog );

        switch( reply ) {
        case mpc_dnptitle:
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
            else {
                setCommand( mpcontrol, mpc_play );
            }

            break;

        case mpc_quit:
        	setCommand(mpcontrol, mpc_quit );
        	break;

        case mpc_repl:
            setCommand( mpcontrol, mpc_repl );

        default:
            setCommand( mpcontrol, mpc_play );
        }
    }
    else {
        fail( F_WARN, "Already paused!" );
    }
}

/**
 * called by the window->close button
 * called by the info->quit menu
 * just sets the mpcontrol command
 */
void destroy( GtkWidget *widget, gpointer   data ) {
	writeConfig( mpcontrol );
    setCommand( mpcontrol, mpc_quit );
}

/**
 * settings/info route
 */
void infoStart( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    int reply;

    dialog = gtk_message_dialog_new(
                 GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_QUESTION,
                 GTK_BUTTONS_NONE,
                 "Information" );

    if( mpcontrol->playstream == 0 ) {
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"Application",  1,
								"Database",  2,
								"Quit!", 3,
								NULL );
    }
    else {
        gtk_show_about_dialog ( GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                                "program-name", "gmixplay",
                                "copyright", "2017 B.Weber",
                                "license-type", GTK_LICENSE_MIT_X11,
                                "version", VERSION,
                                "comments", "GTK based front-end to mpg123, planned to replace my old "
                                "squeezebox/squeezeboxserver and act as a radio replacement to play "
                                "background music but stay sleek enough to run on a mini ARM board.",
                                "website", "https://github.com/foxbow/mixplay",
                                NULL, NULL );
        return;
    }

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
                                NULL, NULL );
        break;

    case 2:
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
            progressStart( "Database Info" );
            progressLog( "Music dir: %s\n", mpcontrol->musicdir );
            dumpInfo( mpcontrol->root, -1 );
            progressEnd( "End Database info." );
            break;

        case 2:
            setCommand( mpcontrol, mpc_dbclean );
            break;
        }

        break;

    case 3:
    	writeConfig( mpcontrol );
        setCommand( mpcontrol, mpc_quit );
//        gtk_main_quit();
        break;
    }
}

/**
 * set the active profile/stream when a list item is selected
 * This does *not* change the current state but will prepare any calls to
 * setProfile()
 */
static void activeSelect_cb(GtkTreeSelection *selection, gpointer data ) {
    GtkTreeIter iter;
    GtkTreeModel *model;
    int active;

    if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
            gtk_tree_model_get( model, &iter, 2, &active, -1);
            mpcontrol->active=active;
    }
}

/**
 * invoked by the 'profile' button
 */
void profileStart( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    GtkWidget *msgArea;
    GtkWidget *list;
    GtkListStore *store;
    GtkTreeIter  iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *tselect;

    int i, reply, profile;
    profile=mpcontrol->active;

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

    msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );

    store = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT );

	for( i=0; i < mpcontrol->profiles; i++ ) {
		gtk_list_store_insert_with_values (store, &iter, -1, 0, "P", 1, mpcontrol->profile[i], 2, i+1, -1 );
	}
	for( i=0; i < mpcontrol->streams; i++ ) {
		gtk_list_store_insert_with_values (store, &iter, -1, 0, "C", 1, mpcontrol->sname[i], 2, -(i+1), -1 );
	}

	list=gtk_tree_view_new_with_model( GTK_TREE_MODEL( store ) );
	g_object_unref( G_OBJECT(store));

	tselect = gtk_tree_view_get_selection( GTK_TREE_VIEW( list ) );
	gtk_tree_selection_set_mode( tselect, GTK_SELECTION_BROWSE );
	g_signal_connect( G_OBJECT( tselect ), "changed", G_CALLBACK( activeSelect_cb), NULL );

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes ("Type", renderer, "text", 0, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);
	column = gtk_tree_view_column_new_with_attributes ("Name", renderer, "text", 1, NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (list), column);

	gtk_container_add( GTK_CONTAINER(msgArea), list );
	gtk_widget_show_all(  dialog );

    reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
    gtk_widget_destroy( dialog );

    switch( reply ) {
    case GTK_RESPONSE_OK:
    	if( mpcontrol->active == 0 ) {
    		fail( F_WARN, "No profile active" );
    	}
    	else if( mpcontrol->active != profile ){
    		setCommand( mpcontrol, mpc_profile );
    	}
        break;
    }

}
