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
        if( mpcontrol->current->key != 0 ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"Play",   mpc_play,
									"Replay", mpc_repl,
									"DNP",    mpc_dnptitle,
									"Shuffle",mpc_shuffle,
									"Quit",   mpc_quit,
									NULL );
        }
        else {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"Play",   mpc_play,
									"Replay", mpc_repl,
									"Quit",   mpc_quit,
									NULL );
        }
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
                                    "_Title",  mpc_dnptitle,
                                    "A_lbum",  mpc_dnpalbum,
                                    "_Artist", mpc_dnpartist,
                                    "_Genre",  mpc_dnpgenre,
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

        case mpc_shuffle:
        case mpc_quit:
        	setCommand(mpcontrol, reply );
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
    setCommand( mpcontrol, mpc_quit );
}

/**
 * settings/info route
 */
void infoStart( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    int reply;

	if( mpcontrol->current->key != 0 ) {
		dialog = gtk_message_dialog_new(
                 GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_QUESTION,
                 GTK_BUTTONS_NONE,
                 "Information" );

		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"Application",  1,
								"Database",  2,
								"Clean up database", mpc_dbclean,
//								"Clean up filesystem", mpc_doublets,
								"Quit!", mpc_quit,
								NULL );
	    reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	    gtk_widget_destroy( dialog );
    }
    else {
    	reply=1;
    }

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
    	progressStart( "Database Info" );
    	progressLog( "Music dir: %s\n", mpcontrol->musicdir );
    	dumpInfo( mpcontrol->root, -1 );
    	progressEnd( "End Database info." );
    	break;

    case mpc_doublets:
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_WARNING,
                     GTK_BUTTONS_YES_NO,
                     "This will delete files on your filesystem!\n"
					 "No guarantees are given!");
	    reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	    gtk_widget_destroy( dialog );
        if( reply != GTK_RESPONSE_YES ) {
        	return;
        }

    case mpc_quit:
    case mpc_dbclean:
            setCommand( mpcontrol, reply );
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
    int64_t active;

    if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
            gtk_tree_model_get( model, &iter, 2, &active, -1);
            mpcontrol->active=active;
    }
}

void startSearch() {

}

/**
 * invoked by the 'profile' button
 */
void profileStart( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    GtkWidget *msgArea;
    GtkWidget *list;
    GtkWidget *urlLine;
    GtkListStore *store;
    GtkTreeIter  iter;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *tselect;
    char *path=NULL;

    int i, reply, selected;
    int64_t profile=mpcontrol->active;

    dialog = gtk_message_dialog_new(
                 GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_INFO,
                 GTK_BUTTONS_NONE,
                 "Profiles/Channels" );
    if( profile > 0 ) {
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                                "Okay", GTK_RESPONSE_OK,
    							"Browse", 1,
    							"URL", 2,
								"Search", 3,
                                "Cancel", GTK_RESPONSE_CANCEL,
                                NULL );
    }
    else {
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"Okay", GTK_RESPONSE_OK,
								"Browse", 1,
								"URL", 2,
								"Cancel", GTK_RESPONSE_CANCEL,
								NULL );
    }
    msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );

    store = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT );

	for( i=0; i < mpcontrol->profiles; i++ ) {
		gtk_list_store_insert_with_values (store, &iter, -1, 0, "", 1, mpcontrol->profile[i], 2, i+1, -1 );
	}
	for( i=0; i < mpcontrol->streams; i++ ) {
		gtk_list_store_insert_with_values (store, &iter, -1, 0, ">", 1, mpcontrol->sname[i], 2, -(i+1), -1 );
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
    case 1: // browse filesystem
    	dialog = gtk_file_chooser_dialog_new ( "Select Music",
                                               GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
											   GTK_FILE_CHOOSER_ACTION_OPEN,
											   "_Cancel",
                                               GTK_RESPONSE_CANCEL,
                                               "Open",
                                               GTK_RESPONSE_ACCEPT,
                                               NULL );
    	gtk_dialog_add_button(GTK_DIALOG(dialog), "Play", 1);

        if( mpcontrol->fullscreen ) {
            gtk_window_fullscreen( GTK_WINDOW( dialog ) );
        }

        selected=gtk_dialog_run( GTK_DIALOG ( dialog ) );
        if ( ( selected == GTK_RESPONSE_ACCEPT ) || ( selected == 1 ) ){
        	// Set minimum defaults to let mixplay work
        	path=falloc( MAXPATHLEN, sizeof( char ) );
            strncpy( path, gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) ), MAXPATHLEN );
        }
        gtk_widget_destroy ( dialog );
    	break;
    case 2: // Enter URL
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Open URL" );
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                                "Open", GTK_RESPONSE_OK,
                                "Cancel", GTK_RESPONSE_CANCEL,
                                NULL );

        msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );
        urlLine=gtk_entry_new();
    	gtk_container_add( GTK_CONTAINER(msgArea), urlLine );
    	gtk_widget_show_all(  dialog );
        selected=gtk_dialog_run( GTK_DIALOG( dialog ) );
        if( selected == GTK_RESPONSE_OK ) {
        	path=falloc( MAXPATHLEN, sizeof( char ) );
            strncpy( path, gtk_entry_get_text( GTK_ENTRY( urlLine ) ), MAXPATHLEN );
        }
        gtk_widget_destroy( dialog );
    	break;
    case 3: // search
		mpcontrol->active = profile;
		if( mpcontrol->status == mpc_play ) {
			setCommand(mpcontrol, mpc_stop);
		}
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( mpcontrol->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Search" );
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                				"Title", 't',
								"Artist", 'a',
								"Album", 'l',
                                "Cancel", GTK_RESPONSE_CANCEL,
                                NULL );

        msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );
        urlLine=gtk_entry_new();
    	gtk_container_add( GTK_CONTAINER(msgArea), urlLine );
    	gtk_widget_show_all( dialog );
        selected=gtk_dialog_run( GTK_DIALOG( dialog ) );
        if( selected != GTK_RESPONSE_CANCEL ) {
        	path=falloc( MAXPATHLEN, sizeof( char ) );
        	snprintf( path, MAXPATHLEN, "%c=%s", selected, gtk_entry_get_text( GTK_ENTRY( urlLine ) ) );
        	if( strlen( path ) < 5 ) {
        		fail( F_WARN, "Need at least three characters!\nSucks to be you U2!" );
        		setCommand( mpcontrol, mpc_start );
        	}
        	else {
        		i=searchPlay( mpcontrol->current, path );
        		printver(1, "Found %i titles\n", i );
        		if( i > 0) {
        			setCommand( mpcontrol, mpc_play );
        		}
        		else {
        			setCommand( mpcontrol, mpc_start );
        		}
        	}
        }
        gtk_widget_destroy( dialog );

        if( NULL != path ) {
        	free(path);
        	path=NULL;
        }
    	break;

    case GTK_RESPONSE_OK:
    	if( mpcontrol->active == 0 ) {
    		fail( F_WARN, "No profile active" );
    	}
    	else if( mpcontrol->active != profile ) {
    		setCommand( mpcontrol, mpc_profile );
    	}
    	else {
    		mpcontrol->active = profile;
    	}
    	break;

    default:
		mpcontrol->active = profile;
    }
    if( path != NULL ) {
    	if( setArgument( mpcontrol, path ) ){
        	mpcontrol->active = 0;
    		setCommand( mpcontrol, mpc_start );
    	}
    	free( path );
    }
}
