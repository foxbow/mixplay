/*
 * callbacks.c
 *
 *  Created on: 25.04.2017
 *	  Author: bweber
 */
#include <stdlib.h>
#include <string.h>
#include "gladeutils.h"
#include "player.h"

/**
 * callbacks for the volume button
 */
void setvol( GtkButton *button, gpointer data ) {
	int vol;
	mpconfig *conf=getConfig();
	vol = (int)(gtk_scale_button_get_value ( GTK_SCALE_BUTTON( MP_GLDATA->widgets->volume ) )*100);
	/* make sure a volume command can and needs to be sent */
	if( ( conf->argument == NULL ) && ( vol != conf->volume ) ) {
		conf->volume = vol;
		conf->argument=falloc( 4, sizeof( char ) );
		snprintf( conf->argument, 3, "%i", vol );
		setCommand( mpc_setvol );
	}
}

void incvol( GtkButton *button, gpointer data ) {
	setCommand( mpc_ivol );
}

void decvol( GtkButton *button, gpointer data ) {
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
	mptitle *title;
	int reply;
	mpconfig *mpcontrol=getConfig();

	/* remember the current title even if the player already jumped to the next title */
	title = mpcontrol->current->title;

	dialog = gtk_message_dialog_new(
				 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_MESSAGE_QUESTION,
				 GTK_BUTTONS_NONE,
				 "Mark as favourite\n%s\nAlbum: %s",
				 title->display,
				 title->album );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
							"_Title",  mpc_fav|mpc_title,
							"A_lbum",  mpc_fav|mpc_album,
							"_Artist", mpc_fav|mpc_artist,
							"_Cancel", GTK_RESPONSE_CANCEL,
							NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );

	if( reply != GTK_RESPONSE_CANCEL ) {
		if( mpcontrol->argument != NULL ) {
			addMessage( 0, "Can't mark as DNP, argument is already set! [%s]", mpcontrol->argument );
		}
		else {
			mpcontrol->argument=calloc( sizeof(char), 10);
			snprintf( mpcontrol->argument, 9, "%i", title->key );
			setCommand( reply );
		}
	}
}

/*
 * handles DNP requests
 */
void dnpStart( GtkButton *button, gpointer data ) {
	GtkWidget *dialog;
	int reply;
	mpconfig *mpcontrol=getConfig();
	mptitle  *current = mpcontrol->current->title;

	dialog = gtk_message_dialog_new(
				 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_MESSAGE_QUESTION,
				 GTK_BUTTONS_NONE,
				 "Do not play\n%s\nAlbum: %s\nGenre: %s",
				 current->display,
				 current->album,
				 current->genre );
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
							"_Title",  mpc_dnp|mpc_title,
							"A_lbum",  mpc_dnp|mpc_album,
							"_Artist", mpc_dnp|mpc_artist,
							"_Genre",  mpc_dnp|mpc_genre,
							"_Cancel", GTK_RESPONSE_CANCEL,
							NULL );
	reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
	gtk_widget_destroy( dialog );

	if( reply > 0 ) {
		if( mpcontrol->argument != NULL ) {
			addMessage( 0, "Can't mark as DNP, argument is already set! [%s]", mpcontrol->argument );
		}
		else {
			mpcontrol->argument=calloc( sizeof(char), 10);
			snprintf( mpcontrol->argument, 9, "%i", current->key );
			setCommand( reply );
		}
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
		if( mpcontrol->current->title->key != 0 || mpcontrol->remote ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"Play",   mpc_play,
									"_Replay", mpc_repl,
									"_DNP",	mpc_dnp,
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
		case mpc_dnp:
			dnpStart( button, data );
			break;

		case mpc_quit:
			gtk_main_quit();
			break;

		case mpc_repl:
			setCommand( mpc_repl );
			break;

		}

		if( mpcontrol->status != mpc_play ) {
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

	if( mpcontrol->current->title->key != 0 || mpcontrol->remote ) {
		dialog = gtk_message_dialog_new(
				 GTK_WINDOW( MP_GLDATA->widgets->mixplay_main ),
				 GTK_DIALOG_DESTROY_WITH_PARENT,
				 GTK_MESSAGE_QUESTION,
				 GTK_BUTTONS_NONE,
				 "Musicdir: %s\nDNPSkip: %s\nRepeat: all\nShuffle: on",
				 mpcontrol->musicdir,
		 		( mpcontrol->skipdnp > 0 )?itostr( mpcontrol->skipdnp ):"off" );

		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"_Application",  mpc_idle+1,
/*								"Clean up _filesystem", mpc_doublets, */
								NULL );
		if( !mpcontrol->playstream ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
					"_Database",  mpc_dbinfo,
					"_Clean up database", mpc_dbclean,
					NULL );
		}
		if( mpcontrol->remote ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
					"_Stop server", mpc_QUIT,
					NULL );
		}
		reply=gtk_dialog_run( GTK_DIALOG( dialog ) );
		gtk_widget_destroy( dialog );
	}
	else {
		reply=mpc_idle+1;
	}

	switch( reply ) {
	case mpc_idle+1:
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

	case mpc_dbinfo:
		setCommand( mpc_dbinfo );
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
		setCommand( mpc_doublets );
		break;

	case mpc_QUIT:
		setCommand( mpc_QUIT );
		/* make sure the message is sent.. */
		sleep(1);
		gtk_main_quit();
		break;
	default:
		if( ( reply >= 0 ) && (reply < mpc_idle ) ) {
			setCommand( reply );
		}
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

	/* add global things */
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
							"Okay", GTK_RESPONSE_OK,
							NULL );
	if( profile > 0 )  {
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"_Search", 3,
								NULL );
	}
	if( mpcontrol->remote == 0) {
		if( mpcontrol->root->key == 0 ) {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"_Add", 1,
									NULL );
		}
		else {
			gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
									"_Browse", 1,
									NULL );
		}
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"_URL", 2,
								"_Remote", 5,
								NULL );
	}
	else {
		gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
								"_Locale", 6,
								NULL );
	}
	gtk_dialog_add_buttons( GTK_DIALOG( dialog ),
							"_Cancel", GTK_RESPONSE_CANCEL,
							NULL );


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
			path=falloc( MAXPATHLEN, sizeof( char ) );
			strncpy( path, gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( dialog ) ), MAXPATHLEN );
		}
		gtk_widget_destroy ( dialog );
		if( mpcontrol->root->key == 0 ) {
			mpcontrol->root=recurse( path, mpcontrol->root );
			sfree( &path );
		}

		mpcontrol->active = 0;
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

		mpcontrol->active = 0;
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
			if( strlen( path ) < 3 ) {
				addMessage( 0, "Need at least three characters!\nSucks to be you U2!" );
			}
			else {
				mpcontrol->argument=path;
				setCommand( mpc_search );
				path=NULL; /* is free'd in player! */
			}
		}
		gtk_widget_destroy( dialog );

		mpcontrol->active = profile;
		break;

	case 5: /* remote */
	case 6: /* locale */
		addMessage( 0, "Not supported yet.." );
		mpcontrol->active = profile;
		break;

	case GTK_RESPONSE_OK:
		if( mpcontrol->active == 0 ) {
			addMessage( 0, "No profile selected!" );
			mpcontrol->active = profile;
		}
		else if( mpcontrol->active != profile ) {
			if( mpcontrol->argument != NULL ) {
				addMessage( 0, "Can't change profile, argument is already set! [%s]", mpcontrol->argument );
			}
			else {
				mpcontrol->argument=falloc( 10, sizeof(char) );
				snprintf( mpcontrol->argument, 9, "%i", mpcontrol->active );
				setCommand( mpc_profile );
			}
		}
		break;

	default:
		mpcontrol->active = profile;
	}

	if( ( mpcontrol->active != profile ) && !mpcontrol->remote ) {
		mpcontrol->changed=-1;
	}

	if( path != NULL ) {
		if( mpcontrol->argument != NULL ) {
			addMessage( 0, "Can't set path, argument is already set! [%s]", mpcontrol->argument );
		}
		else {
			mpcontrol->argument=falloc( strlen( path )+1, sizeof(char) );
			strcpy( mpcontrol->argument, path );
			setCommand( mpc_path );
		}
		free( path );
	}
}
