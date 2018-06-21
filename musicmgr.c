/**
 * collection of functions to manage a list of titles
 */

#include "musicmgr.h"
#include "mpgutils.h"
#include "utils.h"

#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>

/**
 * compares two strings
 * uses the shorter string as pattern and the longer as the matchset.
 * Used to make sure that a remix is still considered a match, be it original artist or mixer.
 * returns -1 (true) on match and 0 on mismatch
 */
static int checkSim( const char *txt1, const char *txt2 ) {
	if( strlen( txt1 )> strlen( txt2 ) ) {
		return checkMatch( txt1, txt2 );
	}
	else {
		return checkMatch( txt2, txt1 );
	}
}

/**
 * always resets the marked flag and
 * resets the counted and skipped flag on all titles if at least 50% of the titles have been counted
 */
void newCount( mptitle *root ) {
	unsigned int num=0, count=0;
	mptitle *runner = root;

	do {
		num++;

		if( runner-> flags | MP_CNTD ) {
			count++;
		}

		runner=runner->dbnext;
	}
	while( runner != root );

	do {
		activity("Unmarking");
		runner-> flags &= ~MP_MARK;
		if( ( 100*count )/num > 50 ) {
			runner->flags &= ~(MP_CNTD|MP_SKPD);
		}
		runner=runner->dbnext;
	}
	while( runner != root );
}

/**
 * discards a list of titles and frees the memory
 * returns NULL for intuitive calling
 */
mptitle *cleanTitles( mptitle *root ) {
	mptitle *runner=root;

	if( NULL != root ) {
		root->dbprev->dbnext=NULL;

		while( runner != NULL ) {
			activity("Cleaning");
			root=runner->dbnext;
			free( runner );
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
			free( runner );
			runner=root;
		}
	}

	return NULL;
}

/**
 * add a line to a file
 */
void addToFile( const char *path, const char *line ) {
	FILE *fp;
	fp=fopen( path, "a" );

	if( NULL == fp ) {
		addMessage( 0, "Could not open %s for writing ", path );
		return;
	}

	fputs( line, fp );

	if( '\n' != line[strlen( line )] ) {
		fputc( '\n', fp );
	}

	fclose( fp );
}


/**
 * inserts a title into the playlist chain. Creates a new playlist
 * startpoint if no target is set.
 */
mptitle *addToPL( mptitle *title, mptitle *target ) {
	if( title->flags & MP_MARK ) {
		addMessage( 0, "Trying to add %s twice! (%i)", title->display, title->flags );
		return target;
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

/*
 * removes a title from the current playlist chain.
 */
static int remFromPL( mptitle *title ) {
	if( title->plnext == title ) {
		return 0;
	}
	title->plprev->plnext=title->plnext;
	title->plnext->plprev=title->plprev;
	title->plprev=title;
	title->plnext=title;
	title->flags&=~MP_MARK;
	return 1;
}

/**
 * helperfunction for scandir() - just return unhidden directories and links
 */
static int dsel( const struct dirent *entry ) {
	return( ( entry->d_name[0] != '.' ) &&
			( ( entry->d_type == DT_DIR ) || ( entry->d_type == DT_LNK ) ) );
}

/**
 * helperfunction for scandir() - just return unhidden regular music files
 */
static int msel( const struct dirent *entry ) {
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
	v1= atoi( ( *d1 )->d_name );
	v2= atoi( ( *d2 )->d_name );

	if( ( v1 > 0 ) && ( v2 > 0 ) && ( v1 - v2 != 0 ) ) {
		return( v1-v2 );
	}

	return strcasecmp( ( *d1 )->d_name, ( *d2 )->d_name );
}

/**
 * loads all music files in cd into musiclist
 */
static int getMusic( const char *cd, struct dirent ***musiclist ) {
	return scandir( cd, musiclist, msel, tsort );
}

/**
 * loads all directories in cd into dirlist
 */
static int getDirs( const char *cd, struct dirent ***dirlist ) {
	return scandir( cd, dirlist, dsel, alphasort );
}

/*
 * checks if a title entry 'title' matches the search term 'pat'
 * the test is driven by the first two characters in the
 * search term. The first character gives the range (taLgd(p))
 * the second character notes if the search should be
 * exact or fuzzy (=*)
 */
static int matchTitle( mptitle *title, const char* pat ) {
	int fuzzy=0;
	char loname[1024];
	char lopat[1024];

	if( ( '=' == pat[1] ) || ( '*' == pat[1] ) ) {
		strlncpy( lopat, &pat[2], 1024 );

		if( '*' == pat[1] ) {
			fuzzy=-1;
		}

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

		case 'p': /* @obsolete! */
			strlncpy( loname, title->path, 1024 );
			break;

		default:
			addMessage( 0, "Unknown range %c in %s!", pat[0], pat );
			addMessage( 0, "Using display instead", pat[0], pat );
			strlncpy( loname, title->display, 1024 );
			break;
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
 * play the search results next
 */
int searchPlay( mptitle *root, const char *pat, unsigned num, const int global ) {
	mptitle *runner=root;
	mptitle *pos=root;
	int cnt=0;
	
	do {
		activity("searching");
		if( matchTitle( runner, pat) && ( global || !( runner->flags & MP_DNP ) ) ){
			moveEntry( runner, pos );
			pos=runner;
			runner->flags|=(MP_CNTD|MP_SKPD);
			cnt++;
		}
		runner=runner->dbnext;
	} while( ( runner != root ) && ( cnt < num ) );
	
	return cnt;
}

/**
 * range is set via entry:
 * first char:  talgd[p] (Title, Artist, aLbum, Genre, Display[, Path] )
 * second char: =*
 *
 * if the second char is anything else, the default (p=) is used.
 */
static int matchList( mptitle **result, mptitle **base, struct marklist_t *term ) {
	mptitle  *runner=*base;
	int cnt=0;

	if( NULL == *base ) {
		addMessage( 0, "No music in list!" );
		return 0;
	}

	while( NULL != term ) {
		while( runner->dbnext != *base ) {
			activity( "Matching" );

			if( !( runner->flags & ( MP_MARK | MP_DNP ) ) ) {
				if( matchTitle( runner, term->dir ) ) {
					*result=addToPL( runner, *result );
					cnt++;
				}
			}

			runner=runner->dbnext;
		} /*  while( runner->next != base ); */

		runner=runner->dbnext; /* start from the beginning */
		term=term->next;
	}

	addMessage( 1, "Added %i titles", cnt );

	return cnt;
}

/**
 * Search the music database at 'base' for all terms in the
 * marklist 'term' all other titles get marked.
 *
 * return the first entry in the playlist
 */
mptitle *searchList( mptitle *base, struct marklist_t *term ) {
	mptitle  *result=NULL;
	mptitle  *runner=NULL;
	int cnt=0;

	if( NULL == base ) {
		addMessage( 0, "%s: No music loaded", __func__ );
		return NULL;
	}

	cnt = matchList( &result, &base, term );

	addMessage( 1, "Created playlist with %i titles", cnt );

	/* Mark every other title so they won't be shuffled. */
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
		}
		while( runner != result );
	}

	return result?result->plnext:NULL;
}

/**
 * applies the dnplist on a list of titles and marks matching titles
 * if th etitle is part of the playlist it will be removed from the playlist 
 * too. This may lead to double played artists though...
 */
int applyDNPlist( mptitle *base, struct marklist_t *list ) {
	mptitle  *pos = base;
	struct marklist_t *ptr = list;
	int cnt=0;

	if( NULL == base ) {
		addMessage( 0, "%s: No music loaded", __func__ );
		return -1;
	}

	if( NULL == list ) {
		return 0;
	}

	do {
		ptr=list;

		while( ptr ) {
			if( matchTitle( pos, ptr->dir ) ) {
				addMessage( 3, "[D] %s: %s", ptr->dir, pos->display );
				pos->flags |= MP_DNP;
				cnt=cnt+remFromPL(pos);
				break;
			}

			ptr=ptr->next;
		}

		pos=pos->dbnext;
	}
	while( pos != base );

	if( cnt > 1 ) {
		addMessage( 0, "Marked %i titles as DNP", cnt );
	}

	return cnt;
}

/**
 * This function sets the favourite bit on titles found in the given list
 */
int applyFAVlist( mptitle *root, struct marklist_t *favourites ) {
	struct marklist_t *ptr = NULL;
	mptitle *runner=root;
	int cnt=0;

	do {
		activity( "Favourites " );
		ptr=favourites;

		while( ptr ) {
			if( matchTitle( runner, ptr->dir ) ) {
				addMessage( 3, "[F] %s: %s", ptr->dir, runner->display );
				if( !( runner->flags & MP_FAV ) ) {
					runner->flags|=MP_FAV;
					cnt++;
				}
				break;
			}

			ptr=ptr->next;
		}

		runner=runner->dbnext;
	}
	while ( runner != root );

	if( cnt > 1 ) {
		addMessage( 0, "Marked %i favourites", cnt );
	}

	return cnt;
}

/**
 * does the actual loading of a list
 */
struct marklist_t *loadList( const char *path ) {
	FILE *file = NULL;
	struct marklist_t *ptr = NULL;
	struct marklist_t *bwlist = NULL;

	char *buff;
	int cnt=0;

	buff=falloc( MAXPATHLEN, sizeof( char ) );

	file=fopen( path, "r" );

	if( !file ) {
		addMessage( 0, "Could not open %s", path );
		return NULL;
	}

	while( !feof( file ) ) {
		fgets( buff, MAXPATHLEN, file );

		if( buff && strlen( buff ) > 1 ) {
			if( !bwlist ) {
				bwlist=falloc( 1, sizeof( struct marklist_t ) );
				ptr=bwlist;
			}
			else {
				ptr->next=falloc( 1, sizeof( struct marklist_t ) );
				ptr=ptr->next;
			}

			if( !ptr ) {
				addMessage( 0, "Could not add %s", buff );
				goto cleanup;
			}

			strlncpy( ptr->dir, buff, MAXPATHLEN );
			ptr->dir[ strlen( buff )-1 ]=0;
			ptr->next=NULL;
			cnt++;
		}
	}

	addMessage( 2, "Loaded %s with %i entries.", path, cnt );

cleanup:
	free( buff );
	fclose( file );

	return bwlist;
}

/**
 * moves an entry in the playlist
 */
void moveEntry( mptitle *entry, mptitle *pos ) {
	if( pos->plnext == entry ) {
		return;
	}

	if( pos == entry ) {
		return;
	}

	/* Move title that is not part of the current playlist? */
	if( !( entry->flags & MP_MARK ) ) {
		pos=addToPL( entry, pos );
	}
	else {
		/* close gap in original position */
		entry->plnext->plprev=entry->plprev;
		entry->plprev->plnext=entry->plnext;

		/* insert into new position */
		entry->plnext=pos->plnext;
		entry->plprev=pos;

		/* fix links */
		pos->plnext=entry;
		entry->plnext->plprev=entry;
	}
}

/**
 * load a standard m3u playlist into a list of titles that the tools can handle
 */
mptitle *loadPlaylist( const char *path ) {
	FILE *fp;
	int cnt=0;
	mptitle *current=NULL;
	char *buff;
	char titlePath[MAXPATHLEN];

	buff=falloc( MAXPATHLEN, sizeof( char ) );

	fp=fopen( path, "r" );

	if( !fp ) {
		addMessage( 0, "Could not open playlist %s", path );
		return NULL;
	}

	while( !feof( fp ) ) {
		activity( "Loading" );
		buff=fgets( buff, MAXPATHLEN, fp );

		if( buff && ( strlen( buff ) > 1 ) && ( buff[0] != '#' ) ) {
			strip( titlePath, buff, MAXPATHLEN ); /* remove control chars like CR/LF */
			current=insertTitle( current, titlePath );
			/* turn list into playlist too */
			current->plprev=current->dbprev;
			current->plnext=current->dbnext;
		}
	}

	fclose( fp );

	addMessage( 2, "Loaded %s with %i entries.", path, cnt );

	return current->dbnext;
}

/**
 * remove a title from the playlist
 * will return the previous title on success. Will do nothing
 * if the title is the last on in the playlist.
 */
static mptitle *plRemove( mptitle *entry ) {
	mptitle *next=entry;

	if( NULL == entry ) {
		addMessage( 0, "Cannot remove no title!" );
		return entry;
	}

	addMessage( 2, "Removed: %s", entry->display );
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

static mptitle *removeByPatLine( mptitle *base, const char *pattern ) {
	mptitle *runner=base;
	runner=base->plprev;

	addMessage( 1, "Rule: %s", pattern );
	while( runner != base ) {
		if( matchTitle( runner, pattern ) ) {
			runner=plRemove( runner );
		}
		else {
			runner=runner->plprev;
		}
	}

	return plRemove( base );
}

/**
 * Marks an entry as DNP and removes it and additional matching titles
 * from the playlist. Matching is done based on range
 *
 * returns the next item in the list. If the next item is NULL, the previous
 * item will be returned. If entry was the last item in the list NULL will be
 * returned.
 */
mptitle *removeByPattern( mptitle *entry, const char *pat ) {
	char pattern[NAMELEN+2];
	strncpy( pattern, pat, 2 );

	switch( pattern[0] ){

	case 'l':
		strlncpy( &pattern[2], entry->album, NAMELEN );
		break;

	case 'a':
		strlncpy( &pattern[2], entry->artist, NAMELEN );
		break;

	case 'g':
		strlncpy( &pattern[2], entry->genre, NAMELEN );
		break;

	case 't':
		strlncpy( &pattern[2], entry->title, NAMELEN );
		break;

	case 'p':
		strlncpy( &pattern[2], entry->path, NAMELEN );
		break;

	case 'd':
		strlncpy( &pattern[2], entry->display, NAMELEN );
		break;

	default:
		addMessage( 0, "Unknown pattern %s!", pat );
		addMessage( 0, "Using display instead" );
		strlncpy( &pattern[2], entry->display, NAMELEN );
		break;
	}

	return removeByPatLine( entry, pattern );
}

/**
 * Insert an entry into the database list
 */
mptitle *insertTitle( mptitle *base, const char *path ) {
	mptitle *root;

	root = ( mptitle* ) falloc( 1, sizeof( mptitle ) );

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

	/* every title is it's own playlist */
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
static unsigned long countTitles( mptitle *base, const unsigned int inc, const unsigned int exc ) {
	unsigned long cnt=0;
	mptitle *runner=base;

	if( NULL == base ) {
		return 0;
	}

	do {
		activity( "Counting" );

		if( ( ( ( inc == MP_ALL ) || ( runner->flags & inc ) ) &&
				!( runner->flags & exc ) ) ) {
			cnt++;
		}

		runner=runner->dbprev;
	}
	while( runner != base );

	return cnt;
}

/**
 * returns the lowest playcount of the current list
 *
 * global: 0 - ignore titles marked as MP_DNP, -1 - check all titles
 */
unsigned int getLowestPlaycount( mptitle *base, const int global ) {
	mptitle *runner=base;
	unsigned int min=-1;

	do {
		if( global || !( runner->flags & MP_DNP ) ) {
			if( runner->playcount < min ) {
				min=runner->playcount;
			}
		}

		runner=runner->dbnext;
	}
	while( runner != base );

	return min;
}

/**
 * skips the global list until a title is found that has not been marked
 * and is not marked as DNP
 */
static mptitle *skipOver( mptitle *current ) {
	mptitle *marker=current;

	while( marker->flags & ( MP_DNP|MP_MARK ) ) {
		marker=marker->dbnext;

		if( marker == current ) {
			addMessage( 0, "Ran out of titles!" );
			return NULL;
		}
	}

	return marker;
}

/**
 * marks titles that have been skipped for at least level times
 * as DNP so they will not end up in a mix.
 */
int DNPSkip( mptitle *base, const unsigned int level ) {
	mptitle *runner=base;
	unsigned int skipskip=0;

/* Sort out skipped titles */
	do {
		activity( "DNPSkipping" );

		if( runner->skipcount > level ) {
			runner->flags |= MP_DNP;
			addMessage( 2, "Marked %s as DNP after %i skips", runner->display, runner->skipcount );
			skipskip++;
		}

		runner=runner->dbnext;
	}
	while( base != runner );

	addMessage( 1, "Marked %i titles as DNP for being skipped", skipskip );
	return skipskip;
}

/**
 * mix a list of titles into a random order
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in a row
 * - prefers titles with lower playcount
 */
mptitle *shuffleTitles( mptitle *base ) {
	mptitle *end=NULL;
	mptitle *runner=base;
	mptitle *guard=NULL;
	unsigned long num=0;
	unsigned long i;
	unsigned int nameskip=0;
	unsigned int playskip=0;
	unsigned int added=0;
	unsigned int stuffed=0;
	unsigned int cycles=0, maxcycles=0;
	struct timeval tv;
	unsigned long pcount=0;
	char name[NAMELEN], lastname[NAMELEN]="";
	unsigned long skip;

	int valid=0;
	/* 0 - nothing checked
	   1 - namecheck okay
	   2 - playcount okay
	   3 - all okay
	  -1 - stuffing
	*/

	/* improve 'randomization' */
	gettimeofday( &tv,NULL );
	srand( getpid()*tv.tv_sec );

	newCount( base );
	num = countTitles( base, MP_ALL, MP_DNP ); /* |MP_MARK ); */
	addMessage( 0, "Shuffling %i titles", num );

	for( i=0; i<num; i++ ) {
		activity( "Shuffling " );

		/* select a random title from the database */
		skip=RANDOM( num-i );
		/* skip forward until a title is found the is neither DNP nor MARK */
		runner=skipOver(skipTitles( runner, skip, -1 ));

		if( runner->flags & MP_MARK ) {
			addMessage( 0, "%s is marked %i %i/%i!", runner->display, skip, i, num );
			continue;
		}


		if( cycles>maxcycles ) {
			maxcycles=cycles;
		}

		cycles=0;

		if( valid != -1 ) {
			valid=0; /* title has not passed any tests */
		}

		while( ( valid & 3 ) != 3 ) {
			/* First title? That name is valid by default */
			if( 0 == strlen( lastname ) ) {
				valid=3;
			}

			/* title did not pass namecheck */
			if( !( valid&1 ) ) {
				guard=runner;
				strlncpy( name, runner->artist, NAMELEN );

				while( checkSim( name, lastname ) ) {
					activity( "Nameskipping" );
					runner=runner->dbnext;
					runner=skipOver( runner );

					if( guard==runner ) {
						addMessage( 2, "Stopped nameskipping at %i/%i", i, num );
						addMessage( 2, runner->display );
						valid=-1; /* No more alternatives */
						break;
					}

					strlncpy( name, runner->artist, NAMELEN );
				}

				if( ( valid != -1 ) && ( guard != runner ) ) {
					nameskip++;
					valid=1; /* we skipped and need to check playcount */
				}
				else {
					valid |= 1; /* we did not skip, so if playcount was fine it's still fine */
				}
			}

			guard=runner;

			/* title did not pass playcountcheck and we are not in stuffing mode */
			while( (valid & 2 ) != 2 ) {
				if( runner->flags & MP_FAV ) {
					if ( runner-> playcount <= 2*pcount ) {
						valid|=2;
					}
				}
				else {
					if ( runner->playcount <= pcount ) {
						valid|=2;
					}
				}

				if( !( valid & 2 ) ) {
					activity( "Playcountskipping" );
					runner=runner->dbnext;
					runner=skipOver( runner );
					valid=0;

					if( guard == runner ) {
						valid=1;	/* we're back where we started and this one is valid by name */
						pcount++;	/* allow more replays */
						addMessage( 2, "Increasing maxplaycount to %li at %i", pcount, i );
					}
				}
			}

			if( runner != guard ) {
				playskip++;	/* we needed to skip */
			}

			if( ++cycles > 10 ) {
				addMessage( 2, "Looks like we ran into a loop in round %i/%i", i, num );
				cycles=0;
				pcount++;	/* allow replays */
				addMessage( 2, "Increasing maxplaycount to %li", pcount );
			}
		} /* while( valid != 3 ) */

		/* title passed all tests */
		if( valid == 3 ) {
			addMessage( 3, "[+] (%i/%li/%3s) %s", runner->playcount, pcount, ONOFF( runner->flags&MP_FAV ), runner->display );
			strlncpy( lastname, runner->artist, NAMELEN );
			end = addToPL( runner, end );
			added++;
		}

		/* runner needs to be stuffed into the playlist */
		if( valid == -1 ) {
			skip=RANDOM( i );
			guard=skipTitles( end, skip, 0 );
			addMessage( 2, "[*] (%i/%li/%3s) %s", runner->playcount, pcount, ONOFF( runner->flags&MP_FAV ), runner->display );
			addToPL( runner, guard );
			stuffed++;
		}
	}

	addMessage( 1, "Added %i titles", added );
	addMessage( 1, "Skipped %i titles to avoid artist repeats", nameskip );
	addMessage( 1, "Skipped %i titles to keep playrate even (max=%i)", playskip, pcount );
	addMessage( 1, "Stuffed %i titles into playlist", stuffed );
	addMessage( 1, "Had a maximum of %i cycles", maxcycles );

	/* Make sure something valid is returned */
	if( NULL == end ) {
		addMessage( 0, "No titles were inserted!" );
		return NULL;
	}

	return end->plnext;
}

/**
 * skips the given number of titles
 * global - select if titles should be skipped in the playlist or the database
 */
mptitle *skipTitles( mptitle *current, long num, const int global ) {
	if( NULL == current ) {
		return NULL;
	}

	while( num > 0 ) {
		if( global ) {
			current=current->dbnext;
		}
		else {
			current=current->plnext;
		}
		num--;
	}

	while( num < 0 ) {
		if( global ) {
			current=current->dbprev;
		}
		else {
			current=current->plprev;
		}
		num++;
	}

	return current;
}

/*
 * returns the title with the given index
 */
mptitle *getTitle( unsigned int key ) {
	mptitle *root=NULL;
	mptitle *runner=NULL;

	if( key == 0 ) {
		return NULL;
	}

	root=getConfig()->root;
	if( root != NULL ) {
		runner=root;
		do {
			if( runner->key == key ) {
				return runner;
			}
			runner=runner->dbnext;
		} while( runner != root );
	}
	return NULL;
}

/*
 * Steps recursively through a directory and collects all music files in a list
 * curdir: current directory path
 * files:  the list to store filenames in
 * returns the LAST entry of the list. So the next item is the first in the list
 */
mptitle *recurse( char *curdir, mptitle *files ) {
	char dirbuff[2*MAXPATHLEN];
	struct dirent **entry;
	int num, i;

	if( '/' == curdir[strlen( curdir )-1] ) {
		curdir[strlen( curdir )-1]=0;
	}

	addMessage( 3, "Checking %s", curdir );

	/* get all music files */
	num = getMusic( curdir, &entry );

	if( num < 0 ) {
		addMessage( 0, "getMusic failed in %s", curdir );
		return files;
	}

	for( i=0; i<num; i++ ) {
		activity( "Scanning" );
		sprintf( dirbuff, "%s/%s", curdir, entry[i]->d_name );
		files=insertTitle( files, dirbuff );
		free( entry[i] );
	}

	free( entry );

	/* step down subdirectories */
	num=getDirs( curdir, &entry );

	if( num < 0 ) {
		addMessage( 0, "getDirs failed on %s", curdir );
		return files;
	}

	for( i=0; i<num; i++ ) {
		sprintf( dirbuff, "%s/%s", curdir, entry[i]->d_name );
		files=recurse( dirbuff, files );
		free( entry[i] );
	}

	free( entry );

	return files;
}

/**
 * just for debugging purposes!
 */
void dumpTitles( mptitle *root, const int pl ) {
	mptitle *ptr=root;

	if( NULL==root ) {
		printf( "NO LIST!\n" );
		return;
	}

	do {
		printf( "[%04i] %s: %s - %s (%s)\n", ptr->key, ptr->path, ptr->artist, ptr->title, ptr->album );

		if( pl ) {
			ptr=ptr->plnext;
		}
		else {
			ptr=ptr->dbnext;
		}
	}
	while( ptr != root );

	/* addMessage( 0, "END DUMP" ); */
}

/**
 * does a database scan and dumps information about playrate
 * favourites and DNPs
 */
void dumpInfo( mptitle *root, int global, int skip ) {
	mptitle *current=root;
	unsigned int maxplayed=0;
	unsigned int minplayed=-1; /* UINT_MAX; */
	unsigned int pl=0;
	unsigned int skipped=0;
	unsigned int dnp=0;
	unsigned int fav=0;

	do {
		if( current->flags & MP_FAV ) {
			fav++;
		}

		if( current->flags & MP_DNP ) {
			dnp++;
		}

		if( global || !( current->flags &MP_DNP ) ) {
			if( current->playcount < minplayed ) {
				minplayed=current->playcount;
			}

			if( current->playcount > maxplayed ) {
				maxplayed=current->playcount;
			}

			if( current->skipcount > skip ) {
				skipped++;
			}
		}

		if( global ) {
			current=current->dbnext;
		}
		else {
			current=current->plnext;
		}
	}
	while( current != root );

	for( pl=minplayed; pl <= maxplayed; pl++ ) {
		unsigned int pcount=0;
		unsigned int dnpcnt=0;
		unsigned int favcnt=0;
		char line[MAXPATHLEN];

		do {
			if( ( global || !( current->flags & MP_DNP ) ) && ( current->playcount == pl ) ) {
				pcount++;
				if( current->flags & MP_DNP ) {
					dnpcnt++;
				}
				if( current->flags & MP_FAV ) {
					favcnt++;
				}
			}
			if( global ) {
				current=current->dbnext;
			}
			else {
				current=current->plnext;
			}
		} while( current != root );

		if( dnpcnt != pcount ){
			switch( pl ) {
			case 0:
				sprintf( line, "Never played\t%i", pcount);
				break;
			case 1:
				sprintf( line, " Once played\t%i", pcount);
				break;
			case 2:
				sprintf( line, "Twice played\t%i", pcount);
				break;
			default:
				sprintf( line, "%5i times\t%i", pl, pcount);
			}

			if ( favcnt == 0 ) {
				addMessage( 0, "%s", line );
			}
			else {
				if( favcnt < pcount ) {
					addMessage( 0, "%s (%i favs)", line, favcnt );
				}
				else {
					addMessage( 0, "%s (allfavs)", line );
				}
			}
		}
	}

	addMessage( 0, "%4i\tfavourites", fav );

	if( global ) {
		addMessage( 0, "%4i\tdo not plays", dnp );
	}

	addMessage( 0, "%4i\tskipped", skipped );
}

/**
 * Copies the given title to the target and turns the name into 'track###.mp3' with ### being the index.
 * Returns -1 when the target runs out of space.
 */
static int copyTitle( mptitle *title, const char* target, const unsigned int index ) {
	int in=-1, out=-1, rv=-1;
	size_t len;
	char filename[MAXPATHLEN];
	struct stat st;

	snprintf( filename, MAXPATHLEN, "%strack%03i.mp3", target, index );

	in=open( title->path, O_RDONLY );

	if( -1 == in ) {
		addMessage( 0, "Couldn't open %s for reading", title->path );
		goto cleanup;
	}

	if( -1 == fstat( in, &st ) ) {
		addMessage( 0, "Couldn't stat %s", title->path );
		goto cleanup;
	}
	len=st.st_size;

	out=open( filename, O_CREAT|O_WRONLY|O_EXCL );

	if( -1 == out ) {
		addMessage( 0, "Couldn't open %s for writing", filename );
		goto cleanup;
	}

	addMessage( 1, "Copy %s to %s", title->display, filename );

	rv = sendfile( out, in, NULL, len );
	if( rv == -1 ) {
		if( errno == EOVERFLOW ) {
			addMessage( 0, "Target ran out of space!" );
		}
		else{
			addMessage( 0, "Could not copy %s", title->path );
		}
	}

cleanup:
	if( in != -1 ) {
		close( in );
	}
	if( out != -1 ) {
		close( out );
	}

	return rv;
}

/**
 * copies the titles in the current playlist onto the target
 * if fav is true, only favourites are copied
 */
int fillstick( mptitle *root, const char *target, int fav ) {
	unsigned int index=0;
	mptitle *current;

	current=root;

	do {
		activity( "Copy title #%03i", index );
		if( fav && ( current->flags & MP_FAV ) ) {
			if( copyTitle( current, target, index++ ) == -1 ) {
				break;
			}
		}
		current=current->plnext;
	}
	while( current != root );

	addMessage( 0, "Copied %i titles to %s", index, target );
	return index;
}

/*
 * writes the current titles as playlist
 */
int writePlaylist( mptitle *root, const char *path ) {
	mptitle *current=root;
	FILE *fp;
	int count=0;

	fp=fopen( path, "w" );
	if( fp == NULL ) {
		addMessage( 0, "Could not access %s\n%s", path, strerror( errno ) );
		return -1;
	}

	do {
		fprintf( fp, "%s\n", current->path );
		current=current->plnext;
		count++;
	} while( current != root );

	fclose( fp );

	return count;
}

int addRangePrefix( char *line, mpcmd cmd ) {
	line[2]=0;
	line[1]=MPC_ISFUZZY(cmd)?'*':'=';
	switch( MPC_RANGE(cmd) ) {
	case mpc_title:
		line[0]='t';
		break;
	case mpc_artist:
		line[0]='a';
		break;
	case mpc_album:
		line[0]='l';
		break;
	case mpc_genre:
		line[0]='g';
		break;
	case mpc_display:
		line[0]='d';
		break;
	default:
		addMessage( 1, "Unknown range %02x", MPC_RANGE(cmd) >> 8 );
		return -1;
	}

	return 0;
}

/**
 * wrapper to handle FAV and DNP. Uses the given title and the range definitions in cmd
 * to create the proper config line and immediately applies it to the current playlist
 */
int handleRangeCmd( mptitle *title, mpcmd cmd ) {
	char line[MAXPATHLEN];
	struct marklist_t buff;
	int cnt=-1;
	mpconfig *config=getConfig();

	if( addRangePrefix(line, cmd) == 0 ) {
		switch( MPC_RANGE(cmd) ) {
		case mpc_title:
			strcat( line, title->title );
			break;
		case mpc_artist:
			strcat( line, title->artist );
			break;
		case mpc_album:
			strcat( line, title->album );
			break;
		case mpc_genre:
			strcat( line, title->genre );
			break;
		case mpc_display:
			strcat( line, title->display );
			break;
		}
		if( strlen( line ) < 5 ) {
			addMessage( 0, "Not enough info in %s!", line );
			return 0;
		}

		buff.next=NULL;
		strcpy( buff.dir, line );

		if( MPC_CMD(cmd) == mpc_fav ) {
			addToFile( config->favname, line );
			cnt=applyFAVlist( title, &buff );
		}
		else if( MPC_CMD(cmd) == mpc_dnp ) {
			addToFile( config->dnpname, line );
			cnt=applyDNPlist( title, &buff );
		}
	}

	return cnt;
}
