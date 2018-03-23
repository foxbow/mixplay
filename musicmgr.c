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
void addToFile( const char *path, const char *line, const char* prefix ) {
    FILE *fp;
    fp=fopen( path, "a" );

    if( NULL == fp ) {
        fail( errno, "Could not open %s for writing ", path );
    }

    fputs( prefix, fp );
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
            fail( F_FAIL, "Unknown range %c in %s!", pat[0], pat );
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
int searchPlay( mptitle *root, const char *pat, unsigned num ) {
	mptitle *runner=root;
	mptitle *pos=root;
	mptitle *next=root->dbnext;
	int cnt=0;

	while( ( next != root ) && ( cnt < num ) ) {
		if( ( runner->flags & MP_MARK ) && matchTitle( runner, pat) ) {
			moveEntry( runner, pos );
			pos=runner;
			runner->flags|=(MP_CNTD|MP_SKPD);
			cnt++;
		}
		runner=next;
		next=next->dbnext;
	}
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
        fail( F_FAIL, "No music in list!" );
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
        fail( F_FAIL, "%s: No music loaded", __func__ );
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
 */
int applyDNPlist( mptitle *base, struct marklist_t *list ) {
    mptitle  *pos = base;
    struct marklist_t *ptr = list;
    int cnt=0;

    if( NULL == base ) {
        fail( F_FAIL, "%s: No music loaded", __func__ );
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
                cnt++;
                break;
            }

            ptr=ptr->next;
        }

        pos=pos->dbnext;
    }
    while( pos != base );

    addMessage( 1, "Marked %i titles as DNP", cnt );

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

    if( !buff ) {
        fail( errno, "%s: can't alloc buffer", __func__ );
    }

    file=fopen( path, "r" );

    if( !file ) {
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
                fail( errno, "Could not add %s", buff );
            }

            strlncpy( ptr->dir, buff, MAXPATHLEN );
            ptr->dir[ strlen( buff )-1 ]=0;
            ptr->next=NULL;
            cnt++;
        }
    }

    addMessage( 2, "Loaded %s with %i entries.", path, cnt );

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
 * add an entry to a list
 */
struct marklist_t *addToList( const char *line, struct marklist_t **list ) {
    struct marklist_t *entry, *runner;
    entry=falloc( 1, sizeof( struct marklist_t ) );

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

        while( runner->next != NULL ) {
            runner=runner->next;
        }

        runner->next=entry;
    }

    return *list;
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

    if( !buff ) {
        fail( errno, "%s: can't alloc buffer", __func__ );
    }

    fp=fopen( path, "r" );

    if( !fp ) {
        fail( errno, "Couldn't open playlist %s", path );
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

    addMessage( 3, "Loaded %s with %i entries.", path, cnt );

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
        fail( F_FAIL, "plRemove() called with NULL!" );
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
    	fail( F_FAIL, "Unknown pattern %s!", pat );
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
static int countTitles( mptitle *base, const unsigned int inc, const unsigned int exc ) {
    int cnt=0;
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
            fail( F_FAIL, "Ran out of titles!" );
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
    unsigned int num=0;
    unsigned int i;
    unsigned int nameskip=0;
    unsigned int playskip=0;
    unsigned int added=0;
    unsigned int stuffed=0;
    unsigned int cycles=0, maxcycles=0;
    struct timeval tv;
    unsigned long pcount=0;
    char name[NAMELEN], lastname[NAMELEN]="";

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
    addMessage( 2, "Shuffling %i titles", num );

    for( i=0; i<num; i++ ) {
        unsigned long skip;
        activity( "Shuffling " );

        /* select a random title from the database */
        skip=RANDOM( num-i );
        runner=skipTitles( runner, skip, -1 );

        if( runner->flags & MP_MARK ) {
            fail( F_FAIL, "%s is marked %i %i/%i!", runner->display, skip, i, num );
        }


        if( cycles>maxcycles ) {
            maxcycles=cycles;
        }

        cycles=0;

        /* skip forward until a title is found the is neither DNP nor MARK */
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
                    activity( "Nameskipping " );
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
			while( !(valid & 2 ) ) {
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
					activity( "Playcountskipping " );
					runner=runner->dbnext;
					runner=skipOver( runner );
					valid=0;

					if( guard == runner ) {
						valid=1;    /* we're back where we started and this one is valid by name */
						pcount++;   /* allow more replays */
						addMessage( 2, "Increasing maxplaycount to %li at %i", pcount, i );
					}
				}
            }

			if( runner != guard ) {
                playskip++;    /* we needed to skip */
            }

            if( ++cycles > 10 ) {
                addMessage( 2, "Looks like we ran into a loop in round %i/%i", i, num );
                cycles=0;
                pcount++;   /* allow replays */
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

    addMessage( 1, "Added %i titles                          ", added );
    addMessage( 1, "Skipped %i titles to avoid artist repeats", nameskip );
    addMessage( 1, "Skipped %i titles to keep playrate even (max=%i)", playskip, pcount );
    addMessage( 1, "Stuffed %i titles into playlist", stuffed );
    addMessage( 1, "Had a maximum of %i cycles", maxcycles );

    /* Make sure something valid is returned */
    if( NULL == end ) {
        fail( F_FAIL, "No titles were inserted!" );
    }

    return end->plnext;
}

/**
 * skips the given number of titles
 * global - select if titles should be skipped in the playlist or the database
 */
mptitle *skipTitles( mptitle *current, int num, const int global ) {
    int dir=num;
    num=abs( num );

    if( NULL == current ) {
        return NULL;
    }

    if( 0 == num ) {
        if( global ) {
            return skipOver( current );
        }
        else {
            return current;
        }
    }

    while( num > 0 ) {
        if( dir < 0 ) {
            if( global ) {
                current=current->dbprev;
            }
            else {
                current=current->plprev;
            }
        }
        else {
            if( global ) {
                current=skipOver( current->dbnext );
            }
            else {
                current=current->plnext;
            }
        }

        num--;
    }

    return current;
}

/**
 * This function sets the favourite bit on titles found in the given list
 */
int applyFavourites( mptitle *root, struct marklist_t *favourites ) {
    struct marklist_t *ptr = NULL;
    mptitle *runner=root;
    int cnt=0;

    do {
        activity( "Favourites " );
        ptr=favourites;

        while( ptr ) {
            if( matchTitle( runner, ptr->dir ) ) {
                addMessage( 3, "[F] %s: %s", ptr->dir, runner->display );
                runner->flags|=MP_FAV;
                cnt++;
                break;
            }

            ptr=ptr->next;
        }

        runner=runner->dbnext;
    }
    while ( runner != root );

    addMessage( 1, "Marked %i favourites", cnt );

    return cnt;
}

/**
 * marks the current title as favourite and uses range to check
 * if more favourites need to be included
 */
int markFavourite( mptitle *title, int range ) {
    struct marklist_t buff;
    buff.next=NULL;

    switch( range ) {
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
        sprintf( buff.dir, "p=%s", title->path );
        addMessage( 0, "Range path is obsolete!\n%s", title->display );
        break;

    case SL_TITLE:
        sprintf( buff.dir, "t=%s", title->title );
        break;

    case SL_DISPLAY:
        sprintf( buff.dir, "d=%s", title->display );
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
        fail( errno, "getMusic failed in %s", curdir );
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
        fail( errno, "getDirs failed on %s", curdir );
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
        fail( F_FAIL, "NO LIST" );
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

        do {
            if( ( global || !( current->flags & MP_DNP ) ) && ( current->playcount == pl ) ) {
                pcount++;
                if( current->flags & MP_DNP ) {
                	dnpcnt++;
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

        if( pcount > 0 ) {
        	if( dnpcnt > 0 ) {
				switch( pl ) {
				case 0:
					addMessage( 0, "Never played:\t%i titles (DNP: %i)", pcount, dnpcnt );
					break;

				case 1:
					addMessage( 0, " Once played:\t%i titles (DNP: %i)", pcount, dnpcnt );
					break;

				case 2:
					addMessage( 0, "Twice played:\t%i titles (DNP: %i)", pcount, dnpcnt );
					break;

				default:
					addMessage( 0, "%5i\ttimes played:\t%i titles (DNP: %i)", pl, pcount, dnpcnt );
				}
        	}
        	else {
				switch( pl ) {
				case 0:
					addMessage( 0, "Never played:\t%i titles", pcount );
					break;

				case 1:
					addMessage( 0, " Once played:\t%i titles", pcount );
					break;

				case 2:
					addMessage( 0, "Twice played:\t%i titles", pcount );
					break;

				default:
					addMessage( 0, "%5i\ttimes played:\t%i titles", pl, pcount );
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
	int in, out;
	size_t len;
	char filename[MAXPATHLEN];
    struct stat st;

    snprintf( filename, MAXPATHLEN, "%strack%03i.mp3", target, index );

    in=open( title->path, O_RDONLY );

    if( -1 == in ) {
        fail( errno, "Couldn't open %s for reading", title->path );
    }

    if( -1 == fstat( in, &st ) ) {
        fail( errno, "Couldn't stat %s", title->path );
    }
    len=st.st_size;

    out=open( filename, O_CREAT|O_WRONLY|O_EXCL );

    if( -1 == out ) {
        fail( errno, "Couldn't open %s for writing", filename );
    }

    addMessage( 2, "Copy %s to %s", title->display, filename );

    if( -1 == sendfile( out, in, NULL, len ) ) {
    	if( errno == EOVERFLOW ) return -1;
    	fail( errno, "Could not copy %s", title->path );
    }

    close( in );
    close( out );

    return 0;
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

	addMessage( 1, "Copied %i titles to %s", index, target );
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
