/*
 * mpgutils.c
 *
 * interface to libmpg123
 *
 *  Created on: 04.10.2016
 *	  Author: bweber
 */
#include <assert.h>
#include <stdlib.h>
#include <mpg123.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "utils.h"
#include "mpgutils.h"

/* default genres by number */
static const char* const genres[192] = {
	"Blues"
	,"Classic Rock"
	,"Country"
	,"Dance"
	,"Disco"
	,"Funk"
	,"Grunge"
	,"Hip-Hop"
	,"Jazz"
	,"Metal"
	,"New Age"
	,"Oldies"
	,"Other"
	,"Pop"
	,"R&B"
	,"Rap"
	,"Reggae"
	,"Rock"
	,"Techno"
	,"Industrial"
	,"Alternative"
	,"Ska"
	,"Death Metal"
	,"Pranks"
	,"Soundtrack"
	,"Euro-Techno"
	,"Ambient"
	,"Trip-Hop"
	,"Vocal"
	,"Jazz+Funk"
	,"Fusion"
	,"Trance"
	,"Classical"
	,"Instrumental"
	,"Acid"
	,"House"
	,"Game"
	,"Sound Clip"
	,"Gospel"
	,"Noise"
	,"AlternRock"
	,"Bass"
	,"Soul"
	,"Punk"
	,"Space"
	,"Meditative"
	,"Instrumental Pop"
	,"Instrumental Rock"
	,"Ethnic"
	,"Gothic"
	,"Darkwave"
	,"Techno-Industrial"
	,"Electronic"
	,"Pop-Folk"
	,"Eurodance"
	,"Dream"
	,"Southern Rock"
	,"Comedy"
	,"Cult"
	,"Gangsta"
	,"Top 40"
	,"Christian Rap"
	,"Pop/Funk"
	,"Jungle"
	,"Native American"
	,"Cabaret"
	,"New Wave"
	,"Psychadelic"
	,"Rave"
	,"Showtunes"
	,"Trailer"
	,"Lo-Fi"
	,"Tribal"
	,"Acid Punk"
	,"Acid Jazz"
	,"Polka"
	,"Retro"
	,"Musical"
	,"Rock & Roll"
	,"Hard Rock"
	,"Folk"
	,"Folk-Rock"
	,"National Folk"
	,"Swing"
	,"Fast Fusion"
	,"Bebob"
	,"Latin"
	,"Revival"
	,"Celtic"
	,"Bluegrass"
	,"Avantgarde"
	,"Gothic Rock"
	,"Progressive Rock"
	,"Psychedelic Rock"
	,"Symphonic Rock"
	,"Slow Rock"
	,"Big Band"
	,"Chorus"
	,"Easy Listening"
	,"Acoustic"
	,"Humour"
	,"Speech"
	,"Chanson"
	,"Opera"
	,"Chamber Music"
	,"Sonata"
	,"Symphony"
	,"Booty Bass"
	,"Primus"
	,"Porn Groove"
	,"Satire"
	,"Slow Jam"
	,"Club"
	,"Tango"
	,"Samba"
	,"Folklore"
	,"Ballad"
	,"Power Ballad"
	,"Rhythmic Soul"
	,"Freestyle"
	,"Duet"
	,"Punk Rock"
	,"Drum Solo"
	,"Acapella"
	,"Euro-House"
	,"Dance Hall"
	,"Drum & Bass"
	,"Club-House"
	,"Hardcore Techno"
	,"Terror"
	,"Indie"
	,"BritPop"
	,"Negerpunk"
	,"Polsk Punk"
	,"Beat"
	,"Christian Gangsta Rap"
	,"Heavy Metal"
	,"Black Metal"
	,"Crossover"
	,"Contemporary Christian"
	,"Christian Rock"
	,"Merengue"
	,"Salsa"
	,"Thrash Metal"
	,"Anime"
	,"Jpop"
	,"Synthpop"
	,"Abstract"
	,"Art Rock"
	,"Baroque"
	,"Bhangra"
	,"Big Beat"
	,"Breakbeat"
	,"Chillout"
	,"Downtempo"
	,"Dub"
	,"EBM"
	,"Eclectic"
	,"Electro"
	,"Electroclash"
	,"Emo"
	,"Experimental"
	,"Garage"
	,"Global"
	,"IDM"
	,"Illbient"
	,"Industro-Goth"
	,"Jam Band"
	,"Krautrock"
	,"Leftfield"
	,"Lounge"
	,"Math Rock"
	,"New Romantic"
	,"Nu-Breakz"
	,"Post-Punk"
	,"Post-Rock"
	,"Psytrance"
	,"Shoegaze"
	,"Space Rock"
	,"Trop Rock"
	,"World Music"
	,"Neoclassical"
	,"Audiobook"
	,"Audio Theatre"
	,"Neue Deutsche Welle"
	,"Podcast"
	,"Indie Rock"
	,"G-Funk"
	,"Dubstep"
	,"Garage Rock"
	,"Psybient"
};

/**
 * returns the genre from the tag
 * either it's a number or a literal. If it's a number, the
 * predefined tag will be returned otherwise the literal text
 */
static const char *getGenre( const unsigned char num ) {
	if( num > 191 ) {
		return "invalid";
	}

	return genres[num];
}

/*
 * helper to filter out lines that consists only of spaces
 */
static size_t txtlen( const char *line ) {
	size_t ret=0;
	size_t len=strlen( line );
	while( isspace( line[ret] ) && (ret<=len) ) ret++;
	return( len-ret );
}

/**
 * helperfunction to copy V2 tag data
 */
static int tagCopy( char *target, mpg123_string *tag ) {
	if( NULL == tag ){
		addMessage(3, "Empty Tag!");
		return 0;
	}
	if ( txtlen( tag->p ) == 0 ) {
		addMessage(3, ">%s< is only spaces!", tag->p );
		return 0;
	}

	strip( target, tag->p, NAMELEN-1 );
	return strlen(target);
}

/**
 * takes a directory and tries to guess info from the structure
 * Either it's Artist/Album for directories or just the Artist from an mp3
 * Used as base settings in case no mp3 tag info is available
 */
static void genPathName( mptitle_t *entry  ) {
	char *p;
	char curdir[MAXPATHLEN];
	int blen=0;

	blen=strlen( entry->path );

	/* trailing '/' should never happen! */
	if( entry->path[blen] == '/' ) {
		addMessage( 0, "getPathName called with %s", entry->path );
		blen=blen-1;
	}

	strtcpy( curdir, entry->path, MIN( blen+1, MAXPATHLEN-1 ) );

	/* cut off .mp3 */
	if( endsWith( curdir, ".mp3" ) ) {
		curdir[strlen( curdir ) - 4]=0;
	}

	strcpy( entry->artist, "Unknown" );
	strcpy( entry->album, "None" );

	p=strrchr( curdir, '/' );

	if( NULL == p ) {
		addMessage( 1, "Only title for %s", curdir );
		strtcpy( entry->title, curdir, NAMELEN-1 );
	}
	else {
		p[0]=0;
		strtcpy( entry->title, p+1, NAMELEN-1 );
		if( strlen( curdir ) > 1 ) {
			p=strrchr( curdir, '/' );

			if( NULL == p ) {
				strtcpy( entry->artist, curdir, NAMELEN-1 );
			}
			else {
				p[0]=0;
				strtcpy( entry->album, p+1, NAMELEN-1 );
				p=strrchr( curdir, '/' );
				if( NULL != p ) {
					strtcpy( entry->artist, p+1, NAMELEN-1 );
				}
			}
		}
	}
}

/**
 * read tag data from the file
 * todo use the mpg123 provided text conversion functions
 */
static void fillInfo( mpg123_handle *mh, mptitle_t *title ) {
	mpg123_id3v1 *v1;
	mpg123_id3v2 *v2;
	int meta;
	char path[MAXPATHLEN]="";
	char *p, *b;
	int aisset=0;

	/* Set some default values as tag info may be incomplete */
	genPathName( title );

	addMessage( 2, "< %s:\n%s -%s\n%s", title->path, title->artist, title->title, title->album );

	strtcpy( path, fullpath(title->path), MAXPATHLEN-1 );

	if( mpg123_open( mh, path ) != MPG123_OK ) {
		addMessage( 1, "Could not open %s as MP3 file", path );
		return;
	}

	while( mpg123_framebyframe_next( mh ) == MPG123_OK ) {
		meta = mpg123_meta_check( mh );
		if( meta & MPG123_ID3 ) {
			addMessage(3, "Found ID3 tag");
			break;
		}
	}

	if( mpg123_id3( mh, &v1, &v2 ) == MPG123_OK ) {
		/* Prefer v2 tag data if available */
		if( ( v2 != NULL ) && ( v2->title != NULL ) ){
			tagCopy( title->title, v2->title );
			if( tagCopy( title->artist, v2->artist ) ) {
				aisset=-1;
			}
			tagCopy( title->album, v2->album );

			if( v2->genre ) {
				if( '(' == v2->genre->p[0] ) {
					strtcpy( title->genre, getGenre( atoi( &v2->genre->p[1] ) ), NAMELEN-1 );
				}
				else if( ( v2->genre->p[0] == '0' ) || atoi( v2->genre->p ) > 0 ) {
					strtcpy( title->genre, getGenre( atoi( v2->genre->p ) ), NAMELEN-1 );
				}
				else if( strlen( v2->genre->p ) > 0 ) {
					if( tagCopy( title->genre, v2->genre ) == 1 ) {
						addMessage( 1, "%s is a genre? check %s", title->genre, title->path );
					}
				}
				else {
					strcpy( title->genre, "unset" );
				}
			}
			else {
				strcpy( title->genre, "unset" );
			}
			addMessage(3, "V2 %s/%s\n%s", title->artist, title->title, title->album );
		}
		/* otherwise try v1 data */
		else if( v1 != NULL )  {
			if ( txtlen( v1->title ) > 0 ) {
				strip( title->title, v1->title, 32 );
			}

			if( txtlen( v1->artist ) > 1 ) {
				strip( title->artist, v1->artist, 32 );
				aisset=-1;
			}
			if( txtlen( v1->album ) > 1 ) {
				strip( title->album, v1->album, 32 );
			}
			strtcpy( title->genre, getGenre( v1->genre ), NAMELEN-1 );
			addMessage(3, "V1 %s/%s\n%s", title->artist, title->title, title->album );
		}
		else {
			addMessage( 2, "No MP3 tag info for %s", title->title );
		}
	}
	else {
		addMessage( 0, "Tag parse error in %s", title->path );
	}

	/* remove leading title title number first if any */
	if( ( strtol( title->title, &b, 10 ) != 0 ) &&
			( ( strstr( b, " - " ) == b ) || ( strstr( b, " / " ) == b ) ) ) {
		addMessage( 2, "Turning '%s' into '%s'", title->title, b+3 );
		memmove( title->title, b+3, strlen(b+3)+1 );
	}

	/*
	 * check for titles named in the "artist - title" scheme
	 * do not change artist if it has been set by and MP3 tag
	 * remove leading numbers
	 */
	p=strstr( title->title, " - " );
	if( ( p != NULL ) && !aisset ) {
		addMessage( 2, "Splitting %s", title->title );
		strtcpy( title->artist, title->title, NAMELEN-1 );
		p=strstr( title->artist, " - " );
		if( p != NULL ) {
			p[0]=0;
			strtcpy( title->title, p+3, NAMELEN-1 );
		}
		else {
			addMessage( 0, "String changed during strtcpy( %s, %s )!", title->artist, title->title );
		}
	}

	/*
	 * check for titles named in the "artist / title" scheme
	 * do not change artist if it has been set by and MP3 tag
	 * remove leading numbers
	 */

	p=strstr( title->title, " / " );
	if( ( p != NULL ) && !aisset ) {
		addMessage( 2, "Splitting %s", title->title );
		strtcpy( title->artist, title->title, NAMELEN-1 );
		p=strstr( title->artist, " / " );
		if( p != NULL ) {
			p[0]=0;
			strtcpy( title->title, p+3, NAMELEN-1 );
		}
		else {
			addMessage( 0, "String changed during strtcpy( %s, %s )!", title->artist, title->title );
		}
	}

	assert( title->artist != NULL );

	snprintf( title->display, MAXPATHLEN-1, "%s - %s", title->artist, title->title );
	mpg123_close( mh );
	addMessage(3, "> %s/%s\n%s", title->artist, title->title, title->album );
}

/**
 * read tags for a single title
 */
int fillTagInfo( mptitle_t *title ) {
	mpg123_handle* mh;
	/* Do not try to scan non mp3 files */
	if( !isMusic( title->path) ) {
		addMessage( 0, "%s is not an MP3 file!", title->path );
		return 0;
	}
	mpg123_init();
	mh = mpg123_new( NULL, NULL );
/*	mpg123_param( mh, MPG123_VERBOSE, 0, 0.0 ); */
	mpg123_param( mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0 );
	fillInfo( mh, title );
	mpg123_delete( mh );
	mpg123_exit();
	return 0;
}
