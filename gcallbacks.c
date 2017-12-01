/*
 * callbacks.c
 *
 *  Created on: 25.04.2017
 *      Author: bweber
 */
#include <string.h>
#include "gladeutils.h"
#include "player.h"

void voli( GtkButton *button, gpointer data ) {
    setCommand( mpc_ivol );
}

void vold( GtkButton *button, gpointer data ) {
    setCommand( mpc_dvol );
}

/**
 * called by the up button
 * just sets the mpcontrol command
 */
void playprev( GtkButton *button, gpointer data ) {
    setCommand( mpc_prev );
}

/**
 * called by the down button
 * just sets the mpcontrol command
 */
void playnext( GtkButton *button, gpointer data ) {
    setCommand( mpc_next );
}

/**
 * called by the FAV button
 * asks if current title, artist or album should be marked favourite
 * sets the mpcontrol command accordingly
 */
void markfav( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    struct entry_t *title;
    int reply;
    mpconfig *mpcontrol=getConfig();

    title = mpcontrol->current;

    /*
     * Do not pause. This may mess up things, if the current title
     * changes while the requester is still open. Then the next title
     * will be marked. This is not ideal but better than having a pause
     * during play.
     */
    dialog = gtk_message_dialog_new(
                 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
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

    if( reply != GTK_RESPONSE_CANCEL ) {
    	setCommand( reply );
    }
}

/**
 * called by the play/pause button
 * just sets the mpcontrol command
 */
void playPause( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    int reply;
    mpconfig *mpcontrol=getConfig();

    if( mpcontrol->status == mpc_play ) {
        setCommand( mpc_play );

        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Pause" );
        if( mpcontrol->current->key != 0 || mpcontrol->remote ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"Play",   mpc_play,
									"_Replay", mpc_repl,
									"_DNP",    mpc_dnptitle,
									"_Shuffle",mpc_shuffle,
									"_Quit",   mpc_quit,
									NULL );
        }
        else {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"Play",   mpc_play,
									"_Replay", mpc_repl,
									"_Quit",   mpc_quit,
									NULL );
        }
        reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
        gtk_widget_destroy( dialog );

        switch( reply ) {
        case mpc_dnptitle:
            if( mpcontrol->status == mpc_play ) {
                setCommand( mpc_play );
            }

            dialog = gtk_message_dialog_new(
                         GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
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
                setCommand( reply );
            }
            else {
                setCommand( mpc_play );
            }

            break;

        case mpc_quit:
        	gtk_main_quit();
        	break;

        case mpc_shuffle:
        	setCommand( reply );
        	break;

        case mpc_repl:
            setCommand( mpc_repl );
            /* no break */

        default:
            setCommand( mpc_play );
        }
    }
    else { /* someone else paused the player */
        setCommand( mpc_play );
    }
}

/**
 * called by the window->close button
 * called by the info->quit menu
 * just sets the mpcontrol command
 */
void destroy( GtkWidget *widget, gpointer   data ) {
	gtk_main_quit();
    setCommand( mpc_quit ); /* should be redundant.. */
}

static char *itostr( int i ) {
	static char retval[20];
	snprintf( retval, 19, "%i", i );
	return retval;
}

/**
 * settings/info route
 */
void infoStart( GtkButton *button, gpointer data ) {
    GtkWidget *dialog;
    int reply;
    mpconfig *mpcontrol=getConfig();

	if( mpcontrol->current->key != 0 || mpcontrol->remote ) {
		dialog = gtk_message_dialog_new(
                 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_QUESTION,
                 GTK_BUTTONS_NONE,
				 "Musicdir: %s\nDNPSkip: %s\nRepeat: all\nShuffle: on",
				 mpcontrol->musicdir,
	     		( mpcontrol->skipdnp > 0 )?itostr( mpcontrol->skipdnp ):"off" );

		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"_Application",  1,
								"_Database",  2,
								"_Clean up database", mpc_dbclean,
/*								"Clean up _filesystem", mpc_doublets, */
								"_Quit!", mpc_quit,
								NULL );
	    reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	    gtk_widget_destroy( dialog );
    }
    else {
    	reply=1;
    }

    switch( reply ) {
    case 1:
        gtk_show_about_dialog ( GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
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
    	addMessage( 0, "Music dir: %s", mpcontrol->musicdir );
    	dumpInfo( mpcontrol->root, -1, mpcontrol->skipdnp );
    	progressEnd( "End Database info." );
    	break;

    case mpc_doublets:
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
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
        /* no break */

    case mpc_quit:
    	gtk_main_quit();
    	break;

    case mpc_dbclean:
		setCommand( reply );
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
    int32_t active;

    if (gtk_tree_selection_get_selected (selection, &model, &iter))
    {
            gtk_tree_model_get( model, &iter, 2, &active, -1);
            getConfig()->active=active;
    }
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
    int64_t profile;
    mpconfig *mpcontrol=getConfig();
    profile=mpcontrol->active;

    dialog = gtk_message_dialog_new(
                 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                 GTK_DIALOG_DESTROY_WITH_PARENT,
                 GTK_MESSAGE_INFO,
                 GTK_BUTTONS_NONE,
                 "Profiles/Channels" );
    if( profile > 0 ) {
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                                "Okay", GTK_RESPONSE_OK,
    							"_Browse", 1,
    							"_URL", 2,
								"_Search", 3,
								"_Fillstick", 4,
                                "_Cancel", GTK_RESPONSE_CANCEL,
                                NULL );
    }
    else {
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"Okay", GTK_RESPONSE_OK,
								"_Browse", 1,
								"_URL", 2,
								"_Cancel", GTK_RESPONSE_CANCEL,
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
    case 1: /* browse filesystem */
    	dialog = gtk_file_chooser_dialog_new ( "Select Music",
                                               GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
											   GTK_FILE_CHOOSER_ACTION_OPEN,
											   "_Cancel",
                                               GTK_RESPONSE_CANCEL,
                                               "_Open",
                                               GTK_RESPONSE_ACCEPT,
                                               NULL );
    	gtk_dialog_add_button(GTK_DIALOG(dialog), "Play", 1);

        if( MP_GLDATA->fullscreen ) {
            gtk_window_fullscreen( GTK_WINDOW( dialog ) );
        }

        selected=gtk_dialog_run( GTK_DIALOG ( dialog ) );
        if ( ( selected == GTK_RESPONSE_ACCEPT ) || ( selected == 1 ) ){
        	/* Set minimum defaults to let mixplay work */
        	path=falloc( MAXPATHLEN, sizeof( char ) );
            strncpy( path, gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) ), MAXPATHLEN );
        }
        gtk_widget_destroy ( dialog );
    	break;
    case 2: /* Enter URL */
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Open URL" );
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                                "_Open", GTK_RESPONSE_OK,
                                "_Cancel", GTK_RESPONSE_CANCEL,
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
    	/*
    	 * turn this into a general search. So a term will be entered and all searches
    	 * will be run, adding results (if any) to a subentry.
    	 * Finally show the results as a tree and allow selection by header, entry
    	 * or maybe even entries.
    	 *
    	 * Genre remains a special case though
    	 */
    case 3: /* search */
		mpcontrol->active = profile;
        dialog = gtk_message_dialog_new(
                     GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
                     GTK_DIALOG_DESTROY_WITH_PARENT,
                     GTK_MESSAGE_INFO,
                     GTK_BUTTONS_NONE,
                     "Search" );
        gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
                				"_Title", 't',
								"_Artist", 'a',
								"A_lbum", 'l',
								"_Genre", 'g',
                                "_Cancel", GTK_RESPONSE_CANCEL,
                                NULL );

        msgArea=gtk_message_dialog_get_message_area( GTK_MESSAGE_DIALOG( dialog ) );
        urlLine=gtk_entry_new();
    	gtk_container_add( GTK_CONTAINER(msgArea), urlLine );
    	gtk_widget_show_all( dialog );
        selected=gtk_dialog_run( GTK_DIALOG( dialog ) );
        if( selected != GTK_RESPONSE_CANCEL ) {
        	path=falloc( MAXPATHLEN, sizeof( char ) );
        	snprintf( path, MAXPATHLEN, "%c*%s", selected, gtk_entry_get_text( GTK_ENTRY( urlLine ) ) );
        	gtk_widget_destroy( dialog );
        	if( strlen( path ) < 5 ) {
        		addMessage( 0, "Need at least three characters!\nSucks to be you U2!" );
        	}
        	else {
        		/* create selection requester */
        		/* search for t, a, l and add the results to the tree headers */
        		/* allow selection */
        		/* if something was selected: */
        		/* - stop player */
        		/* - move titles after the current title */
        		/* - start player */
        		/* */
        		/* close requester */
        		if( mpcontrol->status == mpc_play ) {
        			setCommand( mpc_stop);
        		}
        		i=searchPlay( mpcontrol->current, path );
        		addMessage( 0, "Found %i titles\n", i );
        		if( i > 0) {
        			setCommand( mpc_play );
        		}
        		else {
        			setCommand( mpc_start );
        		}
        	}
        }
        else {
        	gtk_widget_destroy( dialog );
        }

        if( NULL != path ) {
        	free(path);
        	path=NULL;
        }
    	break;
    case 4: /* Fillstick */
    	dialog = gtk_file_chooser_dialog_new ( "Select Target",
                                               GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
											   GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
											   "_Cancel",
                                               GTK_RESPONSE_CANCEL,
                                               "Favourites",
                                               GTK_RESPONSE_ACCEPT,
                                               NULL );
    	gtk_dialog_add_button(GTK_DIALOG(dialog), "All", 1);

        if( MP_GLDATA->fullscreen ) {
            gtk_window_fullscreen( GTK_WINDOW( dialog ) );
        }

        selected=gtk_dialog_run( GTK_DIALOG ( dialog ) );
        if ( ( selected == GTK_RESPONSE_ACCEPT ) || ( selected == 1 ) ){
        	path=falloc( MAXPATHLEN, sizeof( char ) );
            strncpy( path, gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) ), MAXPATHLEN );
        }
        gtk_widget_destroy ( dialog );

    	progressStart( "Fillstick" );
    	addMessage( 0, "Copying to %s", path );
        fillstick( mpcontrol->current, path, ( selected == GTK_RESPONSE_ACCEPT ) );
    	progressEnd( "Done." );

        if( NULL != path ) {
        	free(path);
        	path=NULL;
        }
    	break;

    case GTK_RESPONSE_OK:
    	if( mpcontrol->active == 0 ) {
    		addMessage( 0, "No profile active" );
    	}
    	else if( mpcontrol->active != profile ) {
    		setCommand( mpc_profile );
    	}
    	else {
    		mpcontrol->active = profile;
    	}
    	break;

    default:
		mpcontrol->active = profile;
    }

    if( mpcontrol->active != profile ) {
    	mpcontrol->changed=-1;
    }

    if( path != NULL ) {
    	if( mpcontrol->status == mpc_start ) {
    		setCommand( mpc_start );
    	}
    	if( setArgument( path ) ){
        	mpcontrol->active = 0;
    		setCommand( mpc_start );
    	}
    	free( path );
    }
}
