/**
 * collection of functions to manage a list of titles
 */

#include "musicmgr.h"
#include "mpgutils.h"
#include "utils.h"

#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

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
char *getGenre( struct entry_t *title ) {
	unsigned int gno;
	gno=atoi( title->genre );
	if( ( 0 == gno ) && ( title->genre[0] != '0' ) ) {
		return title->genre;
	}
	if( gno > 191 ) return "invalid";
	return genres[gno];
}

/**
 * moves *title from the original list and inserts it after *target
 * creates a new target if necessary
 */
static struct entry_t *moveTitle( struct entry_t *title, struct entry_t **target ) {
	// remove title from old list
	title->prev->next=title->next;
	title->next->prev=title->prev;

	if( NULL == *target ) {
		*target=title;
		(*target)->next=*target;
		(*target)->prev=*target;
	}
	else {
		title->next=(*target)->next;
		(*target)->next->prev=title;
		title->prev=*target;
		(*target)->next=title;
	}

	return title;
}

/**
 * helperfunction for scandir() - just return unhidden directories and links
 */
static int dsel( const struct dirent *entry ){
	return( ( entry->d_name[0] != '.' ) &&
			( ( entry->d_type == DT_DIR ) || ( entry->d_type == DT_LNK ) ) );
}

/**
 * helperfunction for scandir() - just return unhidden regular files
 */
static int fsel( const struct dirent *entry ){
	return( ( entry->d_name[0] != '.' ) && ( entry->d_type == DT_REG ) );
}

/**
 * helperfunction for scandir() - just return unhidden regular music files
 */
static int msel( const struct dirent *entry ){
	return( ( entry->d_name[0] != '.' ) &&
			( entry->d_type == DT_REG ) &&
			isMusic( entry->d_name ) );
}

/**
 * helperfunction for scandir() sorts entries numerically first then
 * alphabetical
 */
static int tsort( const struct dirent **d1, const struct dirent **d2 ) {
	int v1, v2;
	v1= atoi( (*d1)->d_name );
	v2= atoi( (*d2)->d_name );
	if( ( v1 > 0 ) && ( v2 > 0 ) ) return( v1-v2 );
	return strcasecmp( (*d1)->d_name, (*d2)->d_name );
}

/**
 * loads all music files in cd into musiclist
 */
static int getMusic( const char *cd, struct dirent ***musiclist ){
	return scandir( cd, musiclist, msel, tsort);
}

/**
 * loads all unhidden files in cd into musiclist
 */
int getFiles( const char *cd, struct dirent ***filelist ){
	return scandir( cd, filelist, fsel, alphasort);
}

/**
 * loads all directories in cd into musiclist
 */
int getDirs( const char *cd, struct dirent ***dirlist ){
	return scandir( cd, dirlist, dsel, alphasort);
}

int matchList( struct entry_t **result, struct entry_t **base, struct bwlist_t *term, int range ) {
	struct entry_t  *runner=*base;
	struct entry_t  *next=NULL;
	int match, ranged=0;
	int cnt=0;

	char *mdesc[]={
			"title"
			,"album"
			,"artist"
			,"genre"
			,"path"
	};

	if( NULL == *base ) {
		fail( F_FAIL, "No music in list!" );
	}

	while( term != NULL ) {
		while( runner->next != *base ){
			activity("Matching");
			switch( range ) {
				case SL_TITLE:
					ranged=0;
					match=checkMatch( runner->title, term->dir );
				break;
				case SL_ALBUM:
					ranged=1;
					match=checkMatch( runner->album, term->dir );
				break;
				case SL_ARTIST:
					ranged=2;
					match=checkMatch( runner->artist, term->dir );
				break;
				case SL_GENRE:
					ranged=3;
					match=checkMatch( getGenre(runner), term->dir );
				break;
				case SL_PATH:
					ranged=4;
					match=checkMatch( runner->path, term->dir );
				break;
				default:
					fail( F_FAIL, "invalid checkMatch call %i", range );
			}

			if( match ) {
				next=runner->next;
				if( runner==*base ) {
					*base=(*base)->next;       // make sure base stays valid after removal
				}

				*result=moveTitle( runner, result );
				cnt++;
				runner=next;
			}
			else {
				runner=runner->next;
			}
		} //  while( runner->next != base );
		runner=runner->next; // start from the beginning
		term=term->next;
	}

	if( getVerbosity() ) printf("Added %i titles by %s \n", cnt, mdesc[ranged] );

	return cnt;
}

struct entry_t *searchList( struct entry_t *base, struct bwlist_t *term, int range ) {
	struct entry_t  *result=NULL;
	int cnt=0;

	if( NULL == base ) {
		fail( F_FAIL, "%s: No music loaded", __func__ );
	}

	if( range & SL_ARTIST ) cnt += matchList( &result, &base, term, SL_ARTIST );
	if( range & SL_TITLE ) cnt += matchList( &result, &base, term, SL_TITLE );
	if( range & SL_ALBUM ) cnt += matchList( &result, &base, term, SL_ALBUM );
	if( range & SL_PATH ) cnt += matchList( &result, &base, term, SL_PATH );
	if( range & SL_GENRE ) cnt += matchList( &result, &base, term, SL_GENRE );

	wipeTitles( base );

	if( getVerbosity() ) printf("Added %i titles\n", cnt );

	return result?result->next:NULL;
}

/**
 * applies the dnplist on a list of titles and removes matching titles
 * from the list
 */
struct entry_t *useDNPlist( struct entry_t *base, struct bwlist_t *list ) {
	struct entry_t  *pos = base;
	struct bwlist_t *ptr = list;
	char loname[NAMELEN+MAXPATHLEN];
	int cnt=0;

	if( NULL == base ) {
		fail( F_FAIL, "%s: No music loaded", __func__ );
	}

	if( NULL == list ) {
		return base;
	}

	do{
		strlncpy( loname, pos->path, NAMELEN+MAXPATHLEN );
		strlncat( loname, pos->display, NAMELEN+MAXPATHLEN-strlen(loname) );

		ptr=list;
		while( ptr ){
			if( strstr( loname, ptr->dir ) ) {
				if( pos == base ) base=base->next;
				pos=removeTitle(pos);
				pos=pos->prev;
				cnt++;
				break;
			}
			ptr=ptr->next;
		}

		if( pos == NULL ) {
			fail( F_FAIL, "%s: List emptied, No more titles..", __func__ );
		}

		pos=pos->next;
	} while( pos != base );

	if( getVerbosity() ) printf("Removed %i titles\n", cnt );

	return base;
}

/**
 * does the actual loading of a list
 */
struct bwlist_t *loadList( const char *path ){
	FILE *file = NULL;
	struct bwlist_t *ptr = NULL;
	struct bwlist_t *bwlist = NULL;

	char *buff;
	int cnt=0;

	buff=calloc( MAXPATHLEN, sizeof(char) );
	if( !buff ) fail( errno, "%s: can't alloc buffer", __func__ );

	file=fopen( path, "r" );
	if( !file ) return NULL;
		// fail("Couldn't open list ", path,  errno);

	while( !feof( file ) ){
		buff=fgets( buff, MAXPATHLEN, file );
		if( buff && strlen( buff ) > 1 ){
			if( !bwlist ){
				bwlist=calloc( 1, sizeof( struct bwlist_t ) );
				ptr=bwlist;
			}else{
				ptr->next=calloc( 1, sizeof( struct bwlist_t ) );
				ptr=ptr->next;
			}
			if( !ptr ) fail( errno, "Could not add %s", buff );
			strlncpy( ptr->dir, buff, MAXPATHLEN );
			ptr->dir[ strlen(buff)-1 ]=0;
			ptr->next=NULL;
			cnt++;
		}
	}

	if( getVerbosity() > 1 ) {
		printf("Loaded %s with %i entries.\n", path, cnt );
	}

	free( buff );
	fclose( file );

	return bwlist;
}

void moveEntry( struct entry_t *entry, struct entry_t *pos ) {
	if( pos->next == entry ) return;
	if( pos == entry ) return;

	// close gap in original position
	entry->next->prev=entry->prev;
	entry->prev->next=entry->next;

	// insert into new position
	entry->next=pos->next->next;
	entry->prev=pos;

	// fix links
	pos->next=entry;
	entry->next->prev=entry;
}

/**
 * add an entry to a list
 */
struct bwlist_t *addToList( const char *line, struct bwlist_t **list ) {
	struct bwlist_t *entry, *runner;
	entry=calloc( 1, sizeof( struct bwlist_t ) );
	if( NULL == entry ) {
		fail( errno, "Could not add searchterm %s", line );
	}
	strlncpy( entry->dir, line, MAXPATHLEN );
	entry->next = NULL;

	if( NULL == *list ) {
		*list=entry;
	}
	else {
		runner=*list;
		while( runner->next != NULL ) runner=runner->next;
		runner->next=entry;
	}

	return *list;
}

/**
 * load a standard m3u playlist into a list of titles that the tools can handle
 */
struct entry_t *loadPlaylist( const char *path ) {
	FILE *fp;
	int cnt=0;
	struct entry_t *current=NULL;
	char *buff;
	char titlePath[MAXPATHLEN];

	buff=calloc( MAXPATHLEN, sizeof(char) );
	if( !buff ) fail( errno, "%s: can't alloc buffer", __func__ );

	fp=fopen( path, "r" );
	if( !fp ) fail( errno, "Couldn't open playlist %s", path );

	while( !feof( fp ) ){
		activity("Loading");
		buff=fgets( buff, MAXPATHLEN, fp );
		if( buff && ( strlen( buff ) > 1 ) && ( buff[0] != '#' ) ){
			strip( titlePath, buff, MAXPATHLEN ); // remove control chars like CR/LF
			current=insertTitle( current, titlePath );
		}
	}
	fclose( fp );

	if( getVerbosity() > 2 ) {
		printf("Loaded %s with %i entries.\n", path, cnt );
	}

	return current->next;
}

/**
 * clean up a list of entries
 */
void wipeTitles( struct entry_t *files ){
	struct entry_t *buff=files;
	if( NULL == files ) return;

	files->prev->next=NULL;

	while( buff != NULL ){
		files=buff;
		buff=buff->next;
		free(files);
	}
}

/**
 * helperfunction to remove an entry from a list of titles
 *
 * returns the next item in the list. If the next item is NULL, the previous
 * item will be returned. If entry was the last item in the list NULL will be
 * returned.
 */
struct entry_t *removeTitle( struct entry_t *entry ) {
	struct entry_t *buff=NULL;

	if( entry == entry->next ) {
		buff=NULL;
	}
	else {
		buff=entry->next;
		entry->prev->next=buff;
		buff->prev=entry->prev;
	}
	free(entry);
	return buff;
}

/**
 * helperfunction to insert an entry into a list of titles
 */
struct entry_t *insertTitle( struct entry_t *base, const char *path ){
	struct entry_t *root;

	root = (struct entry_t*) calloc(1, sizeof(struct entry_t));
	if (NULL == root) {
		fail( errno, "%s: Could not alloc root", __func__);
	}

	if( NULL == base ) {
		base=root;
		base->next=base;
		base->prev=base;
	}
	else {
		root->next=base->next;
		root->prev=base;
		base->next=root;
		root->next->prev=root;
	}

	strncpy( root->path, path, MAXPATHLEN );

	fillTagInfo( "", root );

	return root;
}

/**
 * return the number of titles in the list
 */
int countTitles( struct entry_t *base ) {
	int cnt=0;
	struct entry_t *runner=base;
	if( NULL == base ){
		return 0;
	}

	do {
		cnt++;
		runner=runner->prev;
	} while( runner != base );
	if (getVerbosity() > 1 ) printf("Found %i titles\n", cnt );

	return cnt;
}

unsigned long getLowestPlaycount( struct entry_t *base ) {
	struct entry_t *runner=base;
	unsigned long min=-1;

	do {
		if( runner->played < min ) min=runner->played;
		runner=runner->next;
	} while( runner != base );

	return min;
}

/**
 * mix a list of titles into a random order
 */
struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *end=NULL;
	struct entry_t *runner=NULL;
	struct entry_t *guard=NULL;
	int num=0, skipguard=-1;
	int nameskip=0;
	int playskip=0;
	struct timeval tv;
	unsigned long count=0;
	char name[NAMELEN], lastname[NAMELEN]="";

	int valid=0;

	// improve 'randomization'
	gettimeofday(&tv,NULL);
	srand(getpid()*tv.tv_sec);

	count=getLowestPlaycount( base );
	num = countTitles(base)+1;

	// Stepping through every item
	while( base->next != base ) {
		activity("Shuffling ");
		// select a title at random
		runner=skipTitles( base, RANDOM(num) );
		valid=0; // title has not passed any tests

		if( skipguard ) {
			do {
				// check for duplicates
				if( !(valid&1) && strlen(lastname) ) {
					guard=runner;
					strlncpy( name, runner->artist, NAMELEN );
					while( checkMatch( name, lastname ) ) {
						activity("Nameskipping ");
						runner=runner->next;
						if( guard == runner ) {
							skipguard=0; // No more alternatives
							break;
						}
						strlncpy( name, runner->artist, NAMELEN );
					}
					if( guard != runner ){
						nameskip++;
						valid=1; // we skipped and need to check playcount
					}
					else {
						valid |= 1; // we did not skip, so if playcount was fine it's still fine
					}
				}

				// check for playcount
				guard=runner;

				if( !(valid & 2) ) {
					do {
						if( runner->flags & MP_FAV) {
							if ( runner-> played <= 2*count ) {
								valid=3;
							}
						}
						else {
							if ( runner->played <= count ) {
								valid=3;
							}
						}
						if( !(valid & 2) ){
							activity("Playcountskipping ");
							runner=runner->next;
						}
					} while( (valid != 3) && (runner != guard) );

					if( runner != guard ) {
						playskip++;    // we needed to skip
						valid=2;       // check for double artist
					}
					else{
						if( valid == 1 ) { // we went once through without finding a title
							count++;   // allow replays
						}
					}
				}
			} while( !skipguard && ( valid != 3 ) );
			strlncpy(lastname, runner->artist, NAMELEN );
		}

		// Make sure we stay in the right list
		if(runner == base) {
			base=runner->next;
		}

		end = moveTitle( runner, &end );
		num--;
	}

	// add the last title
	end=moveTitle( base, &end );

	if( getVerbosity() > 1 ) {
		printf("Skipped %i titles to avoid artist repeats\n", nameskip );
		printf("Skipped %i titles to keep playrate even (max=%li)\n", playskip, count );
	}

	return end->next;
}

/**
 * skips the given number of titles
 */
struct entry_t *skipTitles( struct entry_t *current, int num ) {
	int dir=num;
	num=abs(num);

	if( 0 == num ) {
		return current;
	}

	if( NULL == current ){
		return NULL;
	}

	while( num > 0 ) {
		if( dir < 0 ) {
			current=current->prev;
		}
		else {
			current=current->next;
		}
		num--;
	}

	return current;
}

/**
 * This function sets the favourite bit on titles found in the given list
 * a literal comparison is used to identify true favourites
 */
int applyFavourites( struct entry_t *root, struct bwlist_t *favourites ) {
	char loname[MAXPATHLEN];
	struct bwlist_t *ptr = NULL;
	struct entry_t *runner=root;
	int cnt=0;

	do {
		activity("Favourites ");
		strlncpy( loname, runner->path, MAXPATHLEN );
		ptr=favourites;
		while( ptr ){
			if( strstr( loname, ptr->dir ) ){
				runner->flags|=MP_FAV;
				cnt++;
				break;
			}
			ptr=ptr->next;
		}
		runner=runner->next;
	} while ( runner != root );

	if( getVerbosity() ) printf("Marked %i favourites\n", cnt );

	return 0;
}

int mp3Exists( const struct entry_t *title ) {
	return( access( title->path, F_OK ) );
}

/*
 * Steps recursively through a directory and collects all music files in a list
 * curdir: current directory path
 * files:  the list to store filenames in
 * returns the LAST entry of the list. So the next item is the first in the list
 */
struct entry_t *recurse( char *curdir, struct entry_t *files, const char *basedir ) {
	struct entry_t *buff=NULL;
	char dirbuff[MAXPATHLEN];
	struct dirent **entry;
	int num, i;

	if( '/' == curdir[strlen(curdir)-1] ){
		curdir[strlen(curdir)-1]=0;
	}

	if( getVerbosity() > 2 ) {
		printf("Checking %s\n", curdir );
	}
	// get all music files according to the dnplist
	num = getMusic( curdir, &entry );
	if( num < 0 ) {
		fail( errno, "getMusic failed in %s", curdir );
	}
	for( i=0; i<num; i++ ) {
		activity("Scanning");
		strncpy( dirbuff, curdir, MAXPATHLEN );
		if( '/' != dirbuff[strlen(dirbuff)-1] ) {
			strncat( dirbuff, "/", MAXPATHLEN );
		}
		strncat( dirbuff, entry[i]->d_name, MAXPATHLEN );

		buff=(struct entry_t *)calloc(1, sizeof(struct entry_t));
		if(buff == NULL) fail( errno, "%s: Could not alloc buffer", __func__ );

		strncpy( buff->path, dirbuff, MAXPATHLEN );
		fillTagInfo( basedir, buff );
		if( NULL == files ){
			files=buff;
			buff->prev=files;
			buff->next=files;
		}
		else {
			buff->prev=files;
			buff->next=files->next;
			files->next->prev=buff;
			files->next=buff;
		}

		files=buff;
		free(entry[i]);
	}
	free(entry);

	num=getDirs( curdir, &entry );
	if( num < 0 ) {
		fail( errno, "getDirs failed on %s", curdir );
	}
	for( i=0; i<num; i++ ) {
		sprintf( dirbuff, "%s/%s", curdir, entry[i]->d_name );
		files=recurse( dirbuff, files, basedir );
		free(entry[i]);
	}
	free(entry);

	return files;
}

/**
 * 	searches for a given path in the mixplay entry list
 */
struct entry_t *findTitle( struct entry_t *base, const char *path ) {
	struct entry_t *runner;
	if( NULL == base ) return NULL;

	runner=base;
	do {
		if( strstr( runner->path, path ) ) return runner;
		runner=runner->next;
	} while ( runner != base );

	return NULL;
}

/**
 * just for debugging purposes!
 */
void dumpTitles( struct entry_t *root ) {
	struct entry_t *ptr=root;
	if( NULL==root ) fail( F_FAIL, "NO LIST" );
	do {
		printf("[%04i] %s: %s - %s (%s)\n", ptr->key, ptr->path, ptr->artist, ptr->title, ptr->album );
		ptr=ptr->next;
	} while( ptr != root );
	fail( F_FAIL, "END DUMP" );
}
