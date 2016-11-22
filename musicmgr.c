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
static char *getGenre( struct entry_t *title ) {
	unsigned int gno;
	gno=atoi( title->genre );
	if( ( 0 == gno ) && ( title->genre[0] != '0' ) ) {
		return title->genre;
	}
	if( gno > 191 ) return "invalid";
	return genres[gno];
}

/**
 * resets the counted flag on all titles if at least 50% of the titles have been counted
 */
void newCount( struct entry_t *root) {
	unsigned int num=0, count=0;
	struct entry_t *runner = root;

	do {
		num++;
		if( runner-> flags | MP_CNTD ) count++;
		runner=runner->dbnext;
	} while( runner != root );

	if( (100*count)/num > 50 ) {
		do {
			runner->flags &= ~MP_CNTD;
			runner=runner->dbnext;
		} while( runner != root );
	}
}

/**
 * inserts a title into the playlist chain. Creates a new playlist
 * startpoint if no target is set.
 */
static struct entry_t *moveToPL( struct entry_t *title, struct entry_t *target ) {
	if( NULL == target ) {
		title->plnext=title;
		title->plprev=title;
	}
	else {
		title->plnext=target->plnext;
		title->plprev=target;
		target->plnext->plprev=title;
		target->plnext=title;
	}
	title->flags |= MP_MARK;
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
 *
int getFiles( const char *cd, struct dirent ***filelist ){
	return scandir( cd, filelist, fsel, alphasort);
}
*/

/**
 * loads all directories in cd into musiclist
 */
static int getDirs( const char *cd, struct dirent ***dirlist ){
	return scandir( cd, dirlist, dsel, alphasort);
}

static int matchList( struct entry_t **result, struct entry_t **base, struct marklist_t *term, int range ) {
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
		while( runner->dbnext != *base ){
			activity("Matching");
			if( !(runner->flags & ( MP_MARK | MP_DNP ) ) ) {
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
					next=runner->dbnext;
					if( runner==*base ) {
						*base=(*base)->dbnext;       // make sure base stays valid after removal
					}

					*result=moveToPL( runner, *result );
					cnt++;
					runner=next;
				}
				else {
					runner=runner->dbnext;
				}
			}
			else {
				runner=runner->dbnext;
			}
		} //  while( runner->next != base );
		runner=runner->dbnext; // start from the beginning
		term=term->next;
	}

	if( getVerbosity() ) printf("Added %i titles by %s \n", cnt, mdesc[ranged] );

	return cnt;
}

struct entry_t *searchList( struct entry_t *base, struct marklist_t *term, int range ) {
	struct entry_t  *result=NULL;
	int cnt=0;

	if( NULL == base ) {
		fail( F_FAIL, "%s: No music loaded", __func__ );
	}

	if( range & SL_ARTIST ) cnt += matchList( &result, &base, term, SL_ARTIST );
	if( range & SL_TITLE )  cnt += matchList( &result, &base, term, SL_TITLE );
	if( range & SL_ALBUM )  cnt += matchList( &result, &base, term, SL_ALBUM );
	if( range & SL_PATH )   cnt += matchList( &result, &base, term, SL_PATH );
	if( range & SL_GENRE )  cnt += matchList( &result, &base, term, SL_GENRE );

	if( getVerbosity() ) printf("Created playlist with %i titles\n", cnt );

	return result?result->plnext:NULL;
}

/**
 * applies the dnplist on a list of titles and marks matching titles
 */
struct entry_t *useDNPlist( struct entry_t *base, struct marklist_t *list ) {
	struct entry_t  *pos = base;
	struct marklist_t *ptr = list;
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
//				if( pos == base ) base=base->dbnext; // check if this makes sense
				pos->flags |= MP_DNP;
				cnt++;
				break;
			}
			ptr=ptr->next;
		}

		pos=pos->dbnext;
	} while( pos != base );

	if( getVerbosity() ) printf("Marked %i titles as DNP\n", cnt );

	return base;
}

/**
 * does the actual loading of a list
 */
struct marklist_t *loadList( const char *path ){
	FILE *file = NULL;
	struct marklist_t *ptr = NULL;
	struct marklist_t *bwlist = NULL;

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
				bwlist=calloc( 1, sizeof( struct marklist_t ) );
				ptr=bwlist;
			}else{
				ptr->next=calloc( 1, sizeof( struct marklist_t ) );
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

/**
 * moves an entry in the playlist
 */
void moveEntry( struct entry_t *entry, struct entry_t *pos ) {
	if( pos->plnext == entry ) return;
	if( pos == entry ) return;

	// Move title that is not part of the current playlist?
	if( !(entry->flags & MP_MARK ) ) {
		pos=moveToPL( entry, pos );
	}
	else {
		// close gap in original position
		entry->plnext->plprev=entry->plprev;
		entry->plprev->plnext=entry->plnext;

		// insert into new position
		entry->plnext=pos->plnext->plnext;
		entry->plprev=pos;

		// fix links
		pos->plnext=entry;
		entry->plnext->plprev=entry;
	}
}

/**
 * add an entry to a list
 */
struct marklist_t *addToList( const char *line, struct marklist_t **list ) {
	struct marklist_t *entry, *runner;
	entry=calloc( 1, sizeof( struct marklist_t ) );
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
			// turn list into playlist too
			current->plprev=current->dbprev;
			current->plnext=current->dbnext;
		}
	}
	fclose( fp );

	if( getVerbosity() > 2 ) {
		printf("Loaded %s with %i entries.\n", path, cnt );
	}

	return current->dbnext;
}

/**
 * remove a title from the playlist
 * will return the previous title on success. Will do nothing
 * if the title is the last on in the playlist.
 */
static struct entry_t *plRemove( struct entry_t *entry ) {
	struct entry_t *next=entry;
	entry->flags |= MP_DNP;
	if( entry->plnext != entry ) {
		next=entry->plprev;
		entry->plprev->plnext=entry->plnext;
		entry->plnext->plprev=entry->plprev;
		entry->plprev=entry;
		entry->plnext=entry;
	}
	return next;
}

/**
 * compares two titles with regards to the range
 * used for blacklisting based on the current title
 */
static int matchTitle( struct entry_t *entry, struct entry_t *pattern, unsigned int range ) {
	switch( range ) {
		case SL_ALBUM:
			return checkMatch( entry->album, pattern->album );
		break;
		case SL_ARTIST:
			return checkMatch( entry->artist, pattern->artist );
		break;
		case SL_GENRE:
			return checkMatch( entry->genre, pattern->genre );
		break;
		case SL_TITLE:
			return checkMatch( entry->title, pattern->title );
		break;
		default:
			fail( F_FAIL, "Illegal Range %i for matchTitle()", range );
	}
	return 0;
}

/**
 * Marks an entry as DNP and removes it and additional matching titles
 * from the playlist. Matching is done based on rage
 *
 * returns the next item in the list. If the next item is NULL, the previous
 * item will be returned. If entry was the last item in the list NULL will be
 * returned.
 */
struct entry_t *removeFromPL( struct entry_t *entry, const unsigned int range ) {
	struct entry_t *runner=entry;
	struct entry_t *retval=entry;
	switch( range ) {
		case SL_ALBUM:
		case SL_ARTIST:
		case SL_GENRE:
		case SL_TITLE:
			runner=entry->plnext;
			while( runner != entry ) {
				if( matchTitle( runner, entry, range ) ) {
					runner=plRemove( entry );
				}
				else {
					runner=runner->plnext;
				}
			}
			retval=plRemove(entry);
		break;
		case SL_PATH:
			retval=plRemove(entry);
		break;
		default:
			fail( F_FAIL, "Unknown range ID: %i!", range );
	}
	return retval;
}

/**
 * Insert an entry into the database list
 */
struct entry_t *insertTitle( struct entry_t *base, const char *path ){
	struct entry_t *root;

	root = (struct entry_t*) calloc(1, sizeof(struct entry_t));
	if (NULL == root) {
		fail( errno, "%s: Could not alloc root", __func__);
	}

	if( NULL == base ) {
		base=root;
		base->dbnext=base;
		base->dbprev=base;
	}
	else {
		root->dbnext=base->dbnext;
		root->dbprev=base;
		base->dbnext=root;
		root->dbnext->dbprev=root;
	}

	// every title is it's own playlist
	root->plnext = root;
	root->plprev = root;

	strncpy( root->path, path, MAXPATHLEN );

	fillTagInfo( "", root );

	return root;
}

/**
 * return the number of titles in the list
 *
 * inc: MP bitmap of flags to include, MP_ALL for all
 * exc: MP bitmap of flags to exclude, 0 for all
 *
 * MP_DNP|MP_FAV will match any title where either flag is set
 */
static int countTitles( struct entry_t *base, const unsigned int inc, const unsigned int exc ) {
	int cnt=0;
	struct entry_t *runner=base;
	if( NULL == base ){
		return 0;
	}

	do {
		if( ( ( (inc == MP_ALL ) || ( runner->flags & inc ) ) &&
			( ( exc == 0 ) || !(runner->flags & exc ) ) ) ) cnt++;
		runner=runner->dbprev;
	} while( runner != base );

	return cnt;
}

/**
 * returns the lowest playcount of the current list
 *
 * global: 0 - ignore titles marked as MP_DNP, -1 - check all titles
 */
unsigned int getLowestPlaycount( struct entry_t *base, const int global ) {
	struct entry_t *runner=base;
	unsigned int min=-1;

	do {
		if( global || !(runner->flags & MP_DNP) ) {
			if( runner->played < min ) min=runner->played;
		}
		runner=runner->dbnext;
	} while( runner != base );

	return min;
}

/**
 * skips the global list until a title is found that has not been marked
 * and is not marked as DNP
 */
static struct entry_t *skipOver( struct entry_t *current ) {
	struct entry_t *marker=current;

	while( marker->flags & (MP_DNP|MP_MARK) ) {
		marker=marker->dbnext;
		if( marker == current ) fail( F_FAIL, "Ran out of titles!" );
	}

	return marker;
}

/**
 * mix a list of titles into a random order
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in a row
 * - prefers titles with lower playcount
 * + skipcount needs to be considered too
 */
struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *end=NULL;
	struct entry_t *runner=base;
	struct entry_t *guard=NULL;
	unsigned int num=0, skipguard=-1;
	unsigned int i;
	int nameskip=0;
	int playskip=0;
	int insskip=0;
	struct timeval tv;
	unsigned long count=0;
	char name[NAMELEN], lastname[NAMELEN]="";

	int valid=0;

	// improve 'randomization'
	gettimeofday(&tv,NULL);
	srand(getpid()*tv.tv_sec);

	count=getLowestPlaycount( base, 0 );
	num = countTitles(base, MP_ALL, MP_DNP );
	if (getVerbosity() > 1 ) printf("Shuffling %i titles\n", num );

	for( i=0; i<num; i++ ) {
		activity("Shuffling ");
		runner=skipTitles( runner, RANDOM(num), -1 );
		runner=skipOver( runner );
		valid=0; // title has not passed any tests

		if( skipguard ) {
			do {
				// check for duplicates
				if( !(valid&1) && strlen(lastname) ) {
					guard=runner;
					strlncpy( name, runner->artist, NAMELEN );
					while( checkMatch( name, lastname ) ) {
						activity("Nameskipping ");
						runner=runner->dbnext;
						runner=skipOver( runner );
						if( guard==runner ) {
							if( getVerbosity() > 2 ) {
								printf( "Stopped nameskipping at %i/%i\n%s\n", i, num, runner->display );
							}
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
							runner=runner->dbnext;
							runner=skipOver( runner );
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
			end = moveToPL( runner, end );
		} // if( skipguard )
		else {
			// find a random position
			valid=0;
			while( !valid ) {
				activity("Stuffing");
				guard=skipTitles( guard, RANDOM(num), -1 );
				if( !checkMatch( runner->artist, runner->artist )
					&& !checkMatch( runner->plnext->artist, runner->artist ) ) {
					valid=-1;
				}
			}
			insskip++;
			guard=moveToPL( runner, guard );
		}
	}

	if( getVerbosity() > 1 ) {
		printf("Skipped %i titles to avoid artist repeats\n", nameskip );
		printf("Skipped %i titles to keep playrate even (max=%li)\n", playskip, count );
		printf("Stuffed %i titles into playlist\n", insskip );
	}

	return end->plnext;
}

/**
 * skips the given number of titles
 * global - select if titles should be skipped in the playlist or the database
 */
struct entry_t *skipTitles( struct entry_t *current, int num, const int global ) {
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
			if( global ) current=current->dbprev;
			else current=current->plprev;
		}
		else {
			if( global ) current=current->dbnext;
			else current=current->plnext;
		}
		num--;
	}

	return current;
}

/**
 * This function sets the favourite bit on titles found in the given list
 * a literal comparison is used to identify true favourites
 */
int applyFavourites( struct entry_t *root, struct marklist_t *favourites ) {
	char loname[MAXPATHLEN];
	struct marklist_t *ptr = NULL;
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
		runner=runner->dbnext;
	} while ( runner != root );

	if( getVerbosity() ) printf("Marked %i favourites\n", cnt );

	return 0;
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

		files=insertTitle( files, dirbuff );
		/*
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
		*/
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
		runner=runner->dbnext;
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
		ptr=ptr->dbnext;
	} while( ptr != root );
	fail( F_FAIL, "END DUMP" );
}
