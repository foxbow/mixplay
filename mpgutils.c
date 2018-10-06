/*
 * mpgutils.c
 *
 * interface to libmpg123
 *
 *  Created on: 04.10.2016
 *	  Author: bweber
 */
#include "mpgutils.h"
#include "utils.h"
#include <stdlib.h>
#include <mpg123.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* default genres by number */
char *genres[192] = {
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
static char *getGenre( unsigned char num ) {
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
	while( isspace( line[ret] ) ) ret++;
	return( len-ret );
}	

/**
 * helperfunction to copy V2 tag data
 */
static int tagCopy( char *target, mpg123_string *tag ) {
	if( NULL == tag ){
		return 0;
	}
	if ( txtlen( tag->p ) == 0 ) {
		return 0;
	}

	strip( target, tag->p, NAMELEN );
	return strlen(target);
}

/**
 * takes a directory and tries to guess info from the structure
 * Either it's Artist/Album for directories or just the Artist from an mp3
 * Used as base settings in case no mp3 tag info is available
 */
static void genPathName( mptitle *entry  ) {
	char *p;
	char curdir[MAXPATHLEN];
	int blen=0;

	blen=strlen( entry->path );

	/* trailing '/' should never happen! */
	if( entry->path[blen] == '/' ) {
		addMessage( 0, "getPathName called with %s", entry->path );
		blen=blen-1;
	}

	strlcpy( curdir, entry->path, MIN( blen+1, MAXPATHLEN ) );

	/* cut off .mp3 */
	if( endsWith( curdir, ".mp3" ) ) {
		curdir[strlen( curdir ) - 4]=0;
	}

	strcpy( entry->artist, "Sampler" );
	strcpy( entry->album, "None" );

	p=strrchr( curdir, '/' );

	if( NULL == p ) {
		addMessage( 1, "Only title for %s", curdir );
		strlcpy( entry->title, curdir, NAMELEN );
	}
	else {
		p[0]=0;
		strlcpy( entry->title, p+1, NAMELEN );
		if( strlen( curdir ) > 1 ) {
			p=strrchr( curdir, '/' );

			if( NULL == p ) {
				strlcpy( entry->artist, curdir, NAMELEN );
			}
			else {
				p[0]=0;
				strlcpy( entry->album, p+1, NAMELEN );
				p=strrchr( curdir, '/' );
				if( NULL == p ) {
					strlcpy( entry->artist, curdir, NAMELEN );
				}
				else {
					strlcpy( entry->artist, p+1, NAMELEN );
				}
			}
		}
	}
	
}

/**
 * read tag data from the file
 */
static void fillInfo( mpg123_handle *mh, mptitle *title ) {
	mpg123_id3v1 *v1;
	mpg123_id3v2 *v2;
	int meta;
	char path[MAXPATHLEN];
	char *p;

	/* Set some default values as tag info may be incomplete */
	genPathName( title );
	
	strlcpy( path, getConfig()->musicdir, MAXPATHLEN );
	strlcat( path, title->path, MAXPATHLEN );
	
	if( mpg123_open( mh, path ) != MPG123_OK ) {
		addMessage( 1, "Could not open %s as MP3 file", path );
		return;
	}

	while( mpg123_framebyframe_next( mh ) == MPG123_OK ) {
		meta = mpg123_meta_check( mh );
		if( meta & MPG123_ID3 ) {
			addMessage(3,"Found ID3 tag");
			break;
		}
	}

	if( mpg123_id3( mh, &v1, &v2 ) == MPG123_OK ) {
		/* Prefer v2 tag data if available */
		if( ( v2 != NULL ) && ( v2->title != NULL ) ){
			tagCopy( title->title, v2->title );
			tagCopy( title->artist, v2->artist );
			tagCopy( title->album, v2->album );

			if( v2->genre ) {
				if( '(' == v2->genre->p[0] ) {
					strlcpy( title->genre, getGenre( atoi( &v2->genre->p[1] ) ), NAMELEN );
				}
				else if( ( v2->genre->p[0] == '0' ) || atoi( v2->genre->p ) > 0 ) {
					strlcpy( title->genre, getGenre( atoi( v2->genre->p ) ), NAMELEN );
				}
				else if( strlen( v2->genre->p ) > 0 ) {
					if( tagCopy( title->genre, v2->genre ) == 1 ) {
						addMessage( 1, "%s is a genre? check %s", title->genre, title->path );
					}
				}
				else {
					strlcpy( title->genre, "unset", 6 );
				}
			}
			else {
				strlcpy( title->genre, "unset", 6 );
			}
		}
		/* otherwise try v1 data */
		else if( v1 != NULL )  {
			if ( txtlen( v1->title ) > 0 ) {
				strip( title->title, v1->title, 32 );
			}

			if( txtlen( v1->artist ) > 1 ) {
				strip( title->artist, v1->artist, 32 );
			}
			if( txtlen( v1->album ) > 1 ) {
				strip( title->album, v1->album, 32 );
			}
			strlcpy( title->genre, getGenre( v1->genre ), NAMELEN );
		}
		else {
			addMessage( 2, "No MP3 tag info for %s", title->title );
		}
	}
	else {
		addMessage( 0, "Tag parse error in %s", title->path );
	}

	/* todo: this can certainly be done more elegant. */	

	/* check for titles named in the "artist - title" scheme */
	p=strstr( title->title, " - " );
	if( p != NULL ) {
		strlcpy( title->artist, title->title, NAMELEN );
		p=strstr( title->artist, " - " );
		p[0]=0;
		strlcpy( title->title, p+3, NAMELEN );
	}

	/* check for titles named in the "artist - title" scheme */
	p=strstr( title->title, " / " );
	if( p != NULL ) {
		strlcpy( title->artist, title->title, NAMELEN );
		p=strstr( title->artist, " / " );
		p[0]=0;
		strlcpy( title->title, p+3, NAMELEN );
	}

	snprintf( title->display, MAXPATHLEN, "%s - %s", title->artist, title->title );
	mpg123_close( mh );
}

/**
 * read tags for a single title
 */
int fillTagInfo( mptitle *title ) {
	mpg123_handle* mh;
	/* Do not try to scan non mp3 files */
	if( !isMusic( title->path) ) return 0;
	mpg123_init();
	mh = mpg123_new( NULL, NULL );
/*	mpg123_param( mh, MPG123_VERBOSE, 0, 0.0 ); */
	mpg123_param( mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0 );
	fillInfo( mh, title );
	mpg123_delete( mh );
	mpg123_exit();
	return 0;
}

