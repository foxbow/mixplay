/*
 * mpgutils.c
 *
 * interface to libmpg123
 *
 *  Created on: 04.10.2016
 *      Author: bweber
 */
#include "mpgutils.h"
#include "utils.h"
#include <stdlib.h>
#include <mpg123.h>
#include <string.h>
#include <stdio.h>

// default genres by number
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
	if( num > 191 ) return "invalid";
	return genres[num];
}

/**
 * helperfunction to copy V2 tag data
 */
static void tagCopy( char *target, mpg123_string *tag ) {
	if( NULL == tag ) return;
	strip( target, tag->p, (tag->fill < NAMELEN)?tag->fill:NAMELEN );
}

/**
 * takes a directory and tries to guess info from the structure
 * Either it's Artist/Album for directories or just the Artist from an mp3
 * Used as base settings in case no mp3 tag info is available
 */
static void genPathName( const char *basedir, struct entry_t *entry  ){
	char *p;
	char curdir[MAXPATHLEN];
	int blen=0;

	blen=strlen(basedir);
	if( basedir[blen] != '/' )  blen=blen+1;

	// Create working copy of the path and cut off trailing /
	strip( curdir, (entry->path)+blen, MAXPATHLEN );

	// cut off .mp3
	if( endsWith( curdir, ".mp3" ) ) {
		curdir[strlen( curdir ) - 4]=0;
	}

	strcpy( entry->artist, "Sampler" );
	strcpy( entry->album, "None" );

	p=strrchr( curdir, '/' );
	if( NULL == p ) {
		strncpy( entry->title, curdir, NAMELEN );
	}
	else {
		p[0]=0;
		strncpy( entry->title, p+1, NAMELEN );

		if( strlen( curdir ) > 1 ) {
			p=strrchr( curdir, '/' );
			if( NULL == p ) {
				strncpy( entry->artist, curdir, NAMELEN );
			}
			else {
				p[0]=0;
				strncpy( entry->album, p+1, NAMELEN );

				p=strrchr( curdir, '/' );
				if( NULL == p ) {
					strncpy( entry->artist, curdir, NAMELEN );
				}
				else {
					strncpy( entry->artist, p+1, NAMELEN );
				}
			}
		}
	}
}

/**
 * read tag data from the file
 */
static void fillInfo( mpg123_handle *mh, const char *basedir, struct entry_t *title ) {
	mpg123_id3v1 *v1;
	mpg123_id3v2 *v2;
	int meta;

	if( ( '@' == title->path[0] ) || isURL( title->path ) ) {
		return;
	}

	genPathName( basedir, title ); // Set some default values as tag info may be incomplete
	if(mpg123_open(mh, title->path ) != MPG123_OK) {
		fail( F_FAIL, "fillInfo(): Cannot open %s: %s\n", title->path, mpg123_strerror(mh) );
	}

	while( mpg123_framebyframe_next( mh ) == MPG123_OK ) {
		meta = mpg123_meta_check(mh);
		if(meta & MPG123_ID3 ) break;
	}

	if( mpg123_id3(mh, &v1, &v2) == MPG123_OK) {
		if( v2 != NULL ) { // Prefer v2 tag data
			tagCopy( title->title, v2->title );
			tagCopy( title->artist, v2->artist );
			tagCopy( title->album, v2->album );
			if( v2->genre ) {
				if( '(' == v2->genre->p[0] ) {
					strncpy( title->genre, getGenre( atoi( &v2->genre->p[1] ) ), NAMELEN );
				}
				else {
					tagCopy( title->genre, v2->genre );
				}
			}
		}
		else if( v1 != NULL ) {
			strip( title->title, v1->title, 32 );
			strip( title->artist, v1->artist, 32 );
			strip( title->album, v1->album, 32 );
			strncpy( title->genre, getGenre( v1->genre ), NAMELEN );
		}
		else {
			printver( 0, "ID3 OK but no tags in %s\n", title->path );
		}
	}
	else {
		printver( 0, "Tag parse error in %s\n", title->path );
	}
	snprintf( title->display, MAXPATHLEN, "%s - %s", title->artist, title->title );
	mpg123_close(mh);
}

/**
 * read tags for a single title
 */
int fillTagInfo( const char *basedir, struct entry_t *title ) {
	mpg123_handle* mh;

	mpg123_init();
	mh = mpg123_new(NULL, NULL);
//	mpg123_param( mh, MPG123_VERBOSE, 0, 0.0 );
	mpg123_param( mh, MPG123_ADD_FLAGS, MPG123_QUIET, 0.0 );
	fillInfo( mh, basedir, title );
	mpg123_delete(mh);
	mpg123_exit();
	return 0;
}
