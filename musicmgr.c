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
 * discards a list of titles and frees the memory
 * returns NULL for intuitive calling
 */
struct entry_t *cleanTitles( struct entry_t *root ) {
	struct entry_t *runner=root;
	if( NULL != root ) {
		root->dbprev->dbnext=NULL;
		while( runner != NULL ) {
			root=runner->dbnext;
			free(runner);
			runner=root;
		}
	}
	return NULL;
}

/**
 * discards a list of searchterms and frees the memory
 * returns NULL for intuitive calling
 */
struct marklist_t *cleanList( struct marklist_t *root ) {
	struct marklist_t *runner=root;
	if( NULL != root ) {
		while( runner != NULL ) {
			root=runner->next;
			free(runner);
			runner=root;
		}
	}
	return NULL;
}

/**
 * add a line to a file
 */
void addToFile( const char *path, const char *line, const char* prefix ) {
	FILE *fp;
	fp=fopen( path, "a" );
	if( NULL == fp ) {
		fail( errno, "Could not open %s for writing ", path );
	}
	fputs( prefix, fp );
	fputs( line, fp );
	if( '\n' != line[strlen(line)] ) {
		fputc( '\n', fp );
	}
	fclose( fp );
}


/**
 * inserts a title into the playlist chain. Creates a new playlist
 * startpoint if no target is set.
 */
struct entry_t *addToPL( struct entry_t *title, struct entry_t *target ) {
	if( title->flags & MP_MARK ) {
		fail( F_FAIL, "Trying to add %s twice! (%i)", title->display, title->flags );
	}

	if( NULL == target ) {
		title->plnext=title;
		title->plprev=title;
	}
	else {
		title->plnext=target->plnext;
		title->plprev=target;
		target->plnext->plprev=title;
		target->plnext=title;
		target->flags |= MP_MARK;
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
 * loads all directories in cd into dirlist
 */
static int getDirs( const char *cd, struct dirent ***dirlist ){
	return scandir( cd, dirlist, dsel, alphasort);
}

/*
 * checks if a title entry 'title' matches the search term 'pat'
 * the test is driven by the first two characters in the
 * search term. The first character gives the range (taLgd(p))
 * the second character notes if the search should be
 * exact or fuzzy (=*)
 */
static int matchTitle( struct entry_t *title, const char* pat ) {
	int fuzzy=0;
	char loname[1024];
	char lopat[1024];

	if( ( '=' == pat[1] ) || ( '*' == pat[1] ) ) {
		strlncpy( lopat, &pat[2], 1024 );
		if( '*' == pat[1] ) fuzzy=-1;

		switch( pat[0] ) {
		case 't':
			strlncpy( loname, title->title, 1024 );
			break;
		case 'a':
			strlncpy( loname, title->artist, 1024 );
			break;
		case 'l':
			strlncpy( loname, title->album, 1024 );
			break;
		case 'g':
			strlncpy( loname, title->genre, 1024 );
			break;
		case 'd':
			strlncpy( loname, title->display, 1024 );
			break;
		case 'p': // @obsolete!
			strlncpy( loname, title->path, 1024 );
			break;
		default:
			fail( F_FAIL, "Unknown range %c in %s!", pat[0], pat );
		}
	}
	else {
		strlncpy( loname, title->display, 1024 );
		strlncpy( lopat, pat, 1024 );
	}

	if( fuzzy ) {
		return checkMatch( loname, lopat );
	}

	else {
		return ( NULL != strstr( loname, lopat ) );
	}
}

/**
 * range is set via entry:
 * first char:  talgd[p] (Title, Artist, aLbum, Genre, Display[, Path] )
 * second char: =*
 *
 * if the second char is anything else, the default (p=) is used.
 */
static int matchList( struct entry_t **result, struct entry_t **base, struct marklist_t *term ) {
	struct entry_t  *runner=*base;
	int cnt=0;

	if( NULL == *base ) {
		fail( F_FAIL, "No music in list!" );
	}

	while( NULL != term ) {
		while( runner->dbnext != *base ){
			activity("Matching");
			if( !(runner->flags & ( MP_MARK | MP_DNP ) ) ) {
				if( matchTitle( runner, term->dir ) ) {
					*result=addToPL( runner, *result );
					cnt++;
				}
			}
			runner=runner->dbnext;
		} //  while( runner->next != base );
		runner=runner->dbnext; // start from the beginning
		term=term->next;
	}

	printver( 1, "Added %i titles\n", cnt );

	return cnt;
}

/**
 * Search the music database at 'base' for all terms in the
 * marklist 'term' all other titles get marked.
 *
 * return the first entry in the playlist
 */
struct entry_t *searchList( struct entry_t *base, struct marklist_t *term ) {
	struct entry_t  *result=NULL;
	struct entry_t  *runner=NULL;
	int cnt=0;

	if( NULL == base ) {
		fail( F_FAIL, "%s: No music loaded", __func__ );
	}

	cnt = matchList( &result, &base, term );

	printver( 1, "Created playlist with %i titles\n", cnt );

	// Mark every other title so they won't be shuffled.
	if( result != NULL ) {
		runner=result;
		do {
			if( runner->plnext == runner ) {
				runner->flags |= MP_MARK;
			}
			else {
				runner->flags &= ~MP_MARK;
			}
			runner=runner->dbnext;
		}while( runner != result );
	}

	return result?result->plnext:NULL;
}

/**
 * applies the dnplist on a list of titles and marks matching titles
 */
int applyDNPlist( struct entry_t *base, struct marklist_t *list ) {
	struct entry_t  *pos = base;
	struct marklist_t *ptr = list;
	int cnt=0;

	if( NULL == base ) {
		fail( F_FAIL, "%s: No music loaded", __func__ );
	}

	if( NULL == list ) {
		return 0;
	}

	do{
		ptr=list;
		while( ptr ){
			if( matchTitle( pos, ptr->dir ) ) {
				pos->flags |= MP_DNP;
				cnt++;
				break;
			}
			ptr=ptr->next;
		}

		pos=pos->dbnext;
	} while( pos != base );

	printver( 1, "Marked %i titles as DNP\n", cnt );

	return cnt;
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

	printver( 2, "Loaded %s with %i entries.\n", path, cnt );

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
		pos=addToPL( entry, pos );
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

	printver( 3, "Loaded %s with %i entries.\n", path, cnt );

	return current->dbnext;
}

/**
 * remove a title from the playlist
 * will return the previous title on success. Will do nothing
 * if the title is the last on in the playlist.
 */
static struct entry_t *plRemove( struct entry_t *entry ) {
	struct entry_t *next=entry;
	if( NULL == entry ) {
		fail( F_FAIL, "plRemove() called with NULL!" );
	}
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
 * Marks an entry as DNP and removes it and additional matching titles
 * from the playlist. Matching is done based on range
 *
 * returns the next item in the list. If the next item is NULL, the previous
 * item will be returned. If entry was the last item in the list NULL will be
 * returned.
 */
struct entry_t *removeFromPL( struct entry_t *entry, const unsigned int range ) {
	struct entry_t *runner=entry;
	struct entry_t *retval=entry;
	char pattern[NAMELEN+2];

	pattern[1]='*';
	switch( range ) {
		case SL_ALBUM:
			pattern[0]='l';
			strlncpy( &pattern[2], entry->album, NAMELEN );
			break;
		case SL_ARTIST:
			pattern[0]='a';
			strlncpy( &pattern[2], entry->artist, NAMELEN );
			break;
		case SL_GENRE:
			pattern[0]='g';
			strlncpy( &pattern[2], entry->genre, NAMELEN );
			break;
		case SL_TITLE:
			pattern[0]='t';
			strlncpy( &pattern[2], entry->title, NAMELEN );
			break;
		case SL_PATH:
			return plRemove(entry);
			break;
		case SL_DISPLAY:
			pattern[0]='d';
			strlncpy( &pattern[2], entry->display, NAMELEN );
			break;
		default:
			fail( F_FAIL, "Unknown range ID: %i!", range );
	}

	runner=entry->plnext;
	while( runner != entry ) {
		if( matchTitle( runner, pattern ) ) {
			runner=plRemove( runner );
		}
		else {
			runner=runner->plnext;
		}
	}
	retval=plRemove(entry);

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
		activity("Counting");
		if( ( ( (inc == MP_ALL ) || ( runner->flags & inc ) ) &&
			!(runner->flags & exc ) ) ) cnt++;
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
 * marks titles that have been skipped for at least level times
 * as DNP so they will not end up in a mix.
 */
int DNPSkip( struct entry_t *base, const unsigned int level ) {
	struct entry_t *runner=base;
	unsigned int skipskip=0;

// Sort out skipped titles
	do {
		activity( "DNPSkipping" );
		if( runner->skipped > level ){
			runner->flags |= MP_DNP;
			printver( 2, "Marked %s as DNP after %i skips\n", runner->display, runner->skipped );
			skipskip++;
		}
		runner=runner->dbnext;
	} while( base != runner );

	printver( 1, "Marked %i titles as DNP for being skipped\n", skipskip );
	return skipskip;
}

/**
 * mix a list of titles into a random order
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in a row
 * - prefers titles with lower playcount
 */
struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *end=NULL;
	struct entry_t *runner=base;
	struct entry_t *guard=NULL;
	unsigned int num=0;
	int skipguard=-1;
	unsigned int i;
	unsigned int nameskip=0;
	unsigned int playskip=0;
	unsigned int insskip=0;
	unsigned int added=0;
	unsigned int cycles=0, maxcycles=0;
	struct timeval tv;
	unsigned long pcount=0;
	char name[NAMELEN], lastname[NAMELEN]="";

	int valid=0;
	/* 0 - nothing checked
	   1 - namecheck okay
	   2 - playcount okay
	   3 - all okay
	*/

	// improve 'randomization'
	gettimeofday(&tv,NULL);
	srand(getpid()*tv.tv_sec);

	num = countTitles(base, MP_ALL, MP_DNP|MP_MARK );
	printver( 2, "Shuffling %i titles\n", num );

	for( i=0; i<num; i++ ) {
		unsigned long skip;
		activity("Shuffling ");
		// select a random title from the database
		skip=RANDOM(num-i);
		runner=skipTitles( runner, skip, -1 );
		if( runner->flags & MP_MARK ) {
			fail( F_FAIL, "%s is marked %i %i/%i!", runner->display, skip, i, num );
		}
		// skip forward until a title is found the is neither DNP nor MARK
		valid=0; // title has not passed any tests

		if( runner->flags & MP_MARK ) {
			fail( F_FAIL, "%s is marked!?", runner->display );
		}

		if( cycles>maxcycles ) maxcycles=cycles;
		cycles=0;
		while( skipguard && ( valid != 3 ) ) {
			// First title? That name is valid by default
			if( 0 == strlen(lastname) ) valid=3;

			if( !(valid&1) ) {
				guard=runner;
				strlncpy( name, runner->artist, NAMELEN );
				while( checkMatch( name, lastname ) ) {
					activity("Nameskipping ");
					runner=runner->dbnext;
					runner=skipOver( runner );
					if( guard==runner ) {
						printver( 2, "\nStopped nameskipping at %i/%i\n%s\n", i, num, runner->display );
						skipguard=0; // No more alternatives
						break;
					}
					strlncpy( name, runner->artist, NAMELEN );
				}
				if( skipguard ) {
					if( guard != runner ){
						nameskip++;
						valid=1; // we skipped and need to check playcount
					}
					else {
						valid |= 1; // we did not skip, so if playcount was fine it's still fine
					}
				}
			}

			// check for playcount
			guard=runner;

			if( !(valid & 2) ) {
				while(1) {
					if( runner->flags & MP_FAV) {
						if ( runner-> played <= 2*pcount ) {
							valid|=2;
							break;
						}
					}
					else {
						if ( runner->played <= pcount ) {
							valid|=2;
							break;
						}
					}

					if( !(valid & 2) ){
						activity("Playcountskipping ");
						runner=runner->dbnext;
						runner=skipOver( runner );
						valid=0;
						if( guard == runner ) {
							pcount++;   // allow replays
							printver( 2, "Increasing maxplaycount to %li at %i\n", pcount, i );
						}
					}
				}

				if( runner != guard ) {
					playskip++;    // we needed to skip
				}
			}

			if( ++cycles > 10 ) {
				printver( 2, "Looks like we ran into a loop in round %i/%i\n", i, num );
				cycles=0;
				// skipguard=0;
				pcount++;   // allow replays
				printver( 2, "Increasing maxplaycount to %li\n", pcount );
			}
		} // while( skipguard && ( valid != 3 ) )

		// title passed all tests
		if( valid == 3 ) {
			printver( 3, "[+] (%i/%li/%3s) %s\n", runner->played, pcount, ONOFF(runner->flags&MP_FAV), runner->display );
			strlncpy(lastname, runner->artist, NAMELEN );
			end = addToPL( runner, end );
			added++;
		}

		// title can't pass tests
		if( !skipguard ) {
			// find a random position
			activity("Stuffing");
			guard=skipTitles( end, RANDOM(num), 0 );
			// @todo: where is the logic error here?
//			while( ( guard->plnext != guard )
//				&& !checkMatch( guard->artist, runner->artist )
//				&& !checkMatch( guard->plnext->artist, runner->artist ) ) {
//				guard=guard->plnext;
//				activity("badend");
//			}
			insskip++;
			printver( 3, "[*] (%i/%li) %s [%s]\n", runner->played, pcount, runner->display, guard->display );
			guard=addToPL( runner, guard );
			added++;
		}
	}

	printver( 2, "Added %i titles                          \n", added );
	printver( 2, "Skipped %i titles to avoid artist repeats\n", nameskip );
	printver( 2, "Skipped %i titles to keep playrate even (max=%i)\n", playskip, pcount );
	printver( 2, "Stuffed %i titles into playlist\n", insskip );
	printver( 2, "Had a maximum of %i cycles\n", maxcycles );

	// Make sure something valid is returned
	if( NULL == end ) {
		fail( F_FAIL, "No titles were inserted!" );
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

	if( NULL == current ){
		return NULL;
	}

	if( 0 == num ) {
		if( global ) return skipOver( current );
		else return current;
	}

	while( num > 0 ) {
		if( dir < 0 ) {
			if( global ) current=current->dbprev;
			else current=current->plprev;
		}
		else {
			if( global ) current=skipOver(current->dbnext);
			else current=current->plnext;
		}
		num--;
	}

	return current;
}

/**
 * This function sets the favourite bit on titles found in the given list
 */
int applyFavourites( struct entry_t *root, struct marklist_t *favourites ) {
	struct marklist_t *ptr = NULL;
	struct entry_t *runner=root;
	int cnt=0;

	do {
		activity("Favourites ");
		ptr=favourites;
		while( ptr ){
			if( matchTitle( runner, ptr->dir ) ){
				runner->flags|=MP_FAV;
				cnt++;
				break;
			}
			ptr=ptr->next;
		}
		runner=runner->dbnext;
	} while ( runner != root );

	printver( 1, "Marked %i favourites\n", cnt );

	return cnt;
}

/**
 * marks the current title as favourite and uses range to check
 * if more favourites need to be included
 */
int markFavourite( struct entry_t *title, int range ) {
	struct marklist_t buff;
	buff.next=NULL;
	switch( range ){
	case SL_ALBUM:
		snprintf( buff.dir, MAXPATHLEN, "l=%s", title->album );
	break;
	case SL_ARTIST:
		snprintf( buff.dir, MAXPATHLEN, "a=%s", title->artist );
	break;
	case SL_GENRE:
		snprintf( buff.dir, MAXPATHLEN, "g*%s", title->genre );
	break;
	case SL_PATH:
		snprintf( buff.dir, MAXPATHLEN, "p=%s", title->path );
		fail( F_WARN, "Range path is obsolete!\n%s", title->display );
	break;
	case SL_TITLE:
		snprintf( buff.dir, MAXPATHLEN, "t=%s", title->title );
	break;
	case SL_DISPLAY:
		snprintf( buff.dir, MAXPATHLEN, "d=%s", title->display );
	break;
	}
	return applyFavourites( title, &buff );
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

	printver( 3, "Checking %s\n", curdir );

	// get all music files
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
		free(entry[i]);
	}
	free(entry);

	// step down subdirectories
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
 * just for debugging purposes!
 */
void dumpTitles( struct entry_t *root, const int pl ) {
	struct entry_t *ptr=root;
	if( NULL==root ) fail( F_FAIL, "NO LIST" );
	do {
		printf("[%04i] %s: %s - %s (%s)\n", ptr->key, ptr->path, ptr->artist, ptr->title, ptr->album );
		if( pl ) ptr=ptr->plnext;
		else ptr=ptr->dbnext;
	} while( ptr != root );
	// fail( F_WARN, "END DUMP" );
}

/**
 * does a database scan and dumps information about playrate
 * favourites and DNPs
 */
void dumpInfo( struct entry_t *root, int global ) {
	struct entry_t *current=root;
	unsigned int maxplayed=0;
	unsigned int minplayed=-1; // UINT_MAX;
	unsigned int pl=0;
	unsigned int skipped=0;
	unsigned int dnp=0;
	unsigned int fav=0;

	do {
		if( current->flags & MP_FAV ) fav++;
		if( current->flags & MP_DNP ) {
			dnp++;
		}
		else {
			if( current->played < minplayed ) minplayed=current->played;
			if( current->played > maxplayed ) maxplayed=current->played;
			if( current->skipped > 0 ) skipped++;
		}
		if( global ) current=current->dbnext;
		else current=current->plnext;
	} while( current != root );

	for( pl=minplayed; pl <= maxplayed; pl++ ) {
		unsigned int pcount=0;
		do {
			if( !( current->flags & MP_DNP ) && ( current->played == pl ) ) pcount++;
			if( global ) current=current->dbnext;
			else current=current->plnext;
		} while( current != root );
		switch( pl ) {
		case 0:
			printver( 0, "Never played:\t%i titles\n", pcount );
			break;
		case 1:
			printver( 0, "Once played:\t%i titles\n", pcount );
			break;
		case 2:
			printver( 0, "Twice played:\t%i titles\n", pcount );
			break;
		default:
			printver( 0, "%i times played:\t%i titles\n", pl, pcount );
		}
	}
	printver( 0, "%i\tfavourites\n", fav );
	if( global ) printver( 0, "%i\tdo not plays\n", dnp );
	printver( 0, "%i\tskipped\n", skipped );
}
