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

#include "database.h"

static char ARTIST_SAMPLER[]="Various";
/*
 * checks if pat has a matching in text. If text is shorter than pat then
 * the test fails.
 */
static int patMatch( const char *text, const char *pat ) {
	char *lotext;
	char *lopat;
	int best=0;
	int res;
	int plen=strlen( pat );
	int tlen=strlen( text );
	int i, j;
	int guard;

	/* The pattern must not be longer than the text! */
	if( tlen < plen ) {
		return 0;
	}

	/* prepare the pattern */
	lopat=(char*)falloc( strlen(pat)+3, 1 );
	strltcpy( lopat+1, pat, plen+1 );
	lopat[0]=-1;
	lopat[plen+1]=-1;

	/* prepare the text */
	lotext=(char*)falloc( tlen+1, 1 );
	strltcpy( lotext, text, tlen+1 );

	/* check */
	for( i=0; i<= ( tlen-plen ); i++ ) {
		res=0;
		for ( j = i; j < plen+i; j++ ) {
			if( ( lotext[j] == lopat[j-i-1] ) ||
				( lotext[j] == lopat[j-i] ) ||
				( lotext[j] == lopat[j-i+1] ) ) {
				res++;
			}
			if( res > best ) {
				best=res;
			}
		}
	}

	/* tweak matchlevel to get along with special cases */
	guard=75+(20*plen)/tlen;

	if( guard > 100 ) guard=100;
	/* compute percentual match */
	res=(100*best)/plen;
	if( res >= guard ) {
		addMessage( 4, "%s - %s - %i/%i", lotext, lopat, res, guard );
	}
	free( lotext );
	free(lopat);
	return ( res >= guard );
}

/*
 * symmetric patMatch that checks if text is shorter than pattern
 * used for title checking in addNewTitle
 */
static int checkSim( const char *text, const char *pat ) {
	if( strlen( text ) < strlen( pat ) ) {
		return patMatch( pat, text );
	}
	else {
		return patMatch( text, pat );
	}
}

/**
 * always resets the marked flag and
 * resets the counted and skipped flag on all titles if at least 50% of the titles have been counted
 */
void newCount( ) {
	mptitle *root=getConfig()->root;
	mptitle *runner = root;
	mpplaylist *pl=getConfig()->current;

	if( pl == NULL ) {
		return;
	}

	do {
		activity("Unmarking");
		runner-> flags &= ~(MP_MARK|MP_CNTD|MP_SKPD);
		runner=runner->next;
	} while( runner != root );

	/*
	 * Mark the last titles to avoid quick repeats.
	 */
	do {
		pl->title->flags |= MP_MARK;
		pl=pl->prev;
	} while ( pl != NULL );
}

/**
 * discards a list of titles and frees the memory
 * returns NULL for intuitive calling
 */
mptitle *wipeTitles( mptitle *root ) {
	mptitle *runner=root;

	if( NULL != root ) {
		root->prev->next=NULL;

		while( runner != NULL ) {
			activity("Cleaning");
			root=runner->next;
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
 * deletes the current playlist
 */
mpplaylist *wipePlaylist( mpplaylist *pl ) {
	mpplaylist *next=NULL;

	if( pl == NULL ) {
		return NULL;
	}

	while( pl->prev != NULL ) {
		pl=pl->prev;
	}

	while( pl != NULL ){
		next=pl->next;
		if( getConfig()->root == NULL ) {
			free( pl->title );
		}
		free(pl);
		pl=next;
	}

	return NULL;
}

mpplaylist *addPLDummy( mpplaylist *pl, const char *name ){
	mpplaylist *buf;
	mptitle *title=(mptitle*)falloc(1, sizeof(mptitle));

	if( pl == NULL ) {
		pl=(mpplaylist*)falloc(1, sizeof(mpplaylist));
	}
	else {
		buf=pl->prev;
		pl->prev=(mpplaylist*)falloc(1,sizeof(mpplaylist));
		pl->prev->prev=buf;
		pl->prev->next=pl;
		if( buf != NULL ) {
			buf->next=pl->prev;
		}
		pl=pl->prev;
	}
	strip( title->display, name, MAXPATHLEN );
	strip( title->title, name, NAMELEN );

	pl->title=title;

	return pl;
}

/**
 * appends a title to the end of a playlist
 * if pl is NULL a new Playlist is created
 * this function either returns pl or the head of the new playlist
 */
mpplaylist *appendToPL( mptitle *title, mpplaylist *pl, const int mark ) {
	mpplaylist *runner=pl;

	if( runner != NULL ) {
		while( runner->next != NULL ) {
			runner=runner->next;
		}
		addToPL( title, runner, mark );
	}
	else {
		pl=addToPL( title, NULL, mark );
	}
	return pl;
}

/*
 * removes a title from the current playlist chain and returns the next title
 */
static mpplaylist *remFromPL( mpplaylist *pltitle ) {
	mpplaylist *ret=pltitle->next;

	if( pltitle->prev != NULL ) {
		pltitle->prev->next=pltitle->next;
	}
	if( pltitle->next != NULL ) {
		pltitle->next->prev=pltitle->prev;
	}
	free(pltitle);
	pltitle->title->flags&=~MP_MARK;

	return ret;
}

/*
 * removes the title with the given key from the playlist
 * this returns NULL or a valid playlist anchor in case the current title was
 * removed
 */
mpplaylist *remFromPLByKey( mpplaylist *root, const unsigned key ) {
	mpplaylist *pl=root;
	if( pl == NULL ) {
		addMessage( 0, "Cannot remove titles from an empty playlist" );
		return NULL;
	}

	if( key == 0 ) {
		return root;
	}

	while( pl->prev != NULL ) {
		pl=pl->prev;
	}

	while( ( pl!=NULL ) && ( pl->title->key != key ) ) {
		pl=pl->next;
	}

	if( pl != NULL ) {
		if( pl->prev != NULL ) {
			pl->prev->next=pl->next;
		}
		if( pl->next!=NULL ) {
			pl->next->prev=pl->prev;
		}
		if( pl == root ) {
			if( pl->next != NULL ) {
				root=pl->next;
			}
			else if( pl->prev != NULL ) {
				root=pl->prev;
			}
			else {
				root=NULL;
			}
		}
		free(pl);
		pl=NULL;
	}
	else {
		addMessage( 0, "No title with key %i in playlist!", key );
	}

	return root;
}

/**
 * inserts a title into the playlist chain. Creates a new playlist
 * if no target is set.
 * This function returns a valid target
 */
mpplaylist *addToPL( mptitle *title, mpplaylist *target, const int mark ) {
	mpplaylist *buf=NULL;

	if( mark && ( title->flags & MP_MARK ) ) {
		addMessage( 0, "Trying to add %s twice! (%i)", title->display, title->flags );
	}

	buf=(mpplaylist*)falloc(1, sizeof( mpplaylist ) );
	memset( buf, 0, sizeof( mpplaylist ) );
	buf->title=title;

	if( NULL == target ) {
		target=buf;
	}
	else {
		buf->next=target->next;
		target->next=buf;
		buf->prev=target;
	}

	if( mark ) {
		title->flags |= MP_MARK;
	}
	return target;
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
 * helperfunction for scandir() - just return unhidden regular music files
 */
static int plsel( const struct dirent *entry ) {
	return( ( entry->d_name[0] != '.' ) &&
			( entry->d_type == DT_REG ) &&
			endsWith( entry->d_name, ".m3u" ) );
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
 * loads all playlists in cd into pllist
 */
int getPlaylists( const char *cd, struct dirent ***pllist ) {
	return scandir( cd, pllist, plsel, tsort );
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
	char *lopat;
	int res=0;

	if( ( '=' == pat[1] ) || ( '*' == pat[1] ) ) {
		if( '*' == pat[1] ) {
			fuzzy=-1;
		}

		switch( pat[0] ) {
		case 't':
			strltcpy( loname, title->title, 1024 );
			break;

		case 'a':
			strltcpy( loname, title->artist, 1024 );
			break;

		case 'l':
			strltcpy( loname, title->album, 1024 );
			break;

		case 'g':
			strltcpy( loname, title->genre, 1024 );
			break;

		case 'd':
			strltcpy( loname, title->display, 1024 );
			break;

		case 'p': /* @obsolete! */
			strltcpy( loname, title->path, 1024 );
			break;

		default:
			addMessage( 0, "Unknown range %c in %s!", pat[0], pat );
			addMessage( 0, "Using display instead" );
			strltcpy( loname, title->display, 1024 );
			break;
		}
	}
	else {
		strltcpy( loname, title->display, 1024 );
	}

	if( fuzzy ) {
		res=patMatch( loname, pat+2 );
	}
	else {
		lopat=(char*)falloc( strlen( pat+1 ), 1 );
		strltcpy( lopat, pat+2, strlen( pat+1 ) );
		res=( strstr( loname, lopat ) != NULL );
		free( lopat );
	}

	return res;
}

/*
 * matches term with pattern in search
 */
static int isMatch( const char *term, const char *pat, const int fuzzy ) {
	char loterm[MAXPATHLEN];

	if( fuzzy ){
		return patMatch( term, pat);
	}
	else {
		strltcpy( loterm, term, MAXPATHLEN );
		return ( strstr( loterm, pat ) != NULL );
	}
}

/**
 * fills the global searchresult structure with the results of the given search.
 * Returns the number of found titles.
 * pat - pattern to search for
 * global - include DNP
 * fill - fill artist and album results
 */
int search( const char *pat, const mpcmd range, const int global ) {
	mptitle *root=getConfig()->root;
	mptitle *runner=root;
	searchresults *res=getConfig()->found;
	unsigned int i=0;
	int found=0;
	unsigned int cnt=0;
	/* free buffer playlist, the arrays will not get lost due to the realloc later */
	res->titles=wipePlaylist(res->titles);
	res->tnum=0;
	res->anum=0;
	res->lnum=0;

	do {
		activity("searching");
		found=0;
		if( global || !(runner->flags & MP_DNP) ) {
			/* check for title/display */
			if( MPC_ISTITLE(range) && isMatch( runner->title, pat, MPC_ISFUZZY(range)  )) {
				found=-1;
			}
			if( MPC_ISDISPLAY( range ) && isMatch( runner->display, pat, MPC_ISFUZZY(range) ) ) {
				found=-1;
			}
			/* check for artist, if title matched, also add artist */
			if( ( found && MPC_EQTITLE( range ) ) || ( MPC_ISARTIST(range) && isMatch( runner->artist, pat, MPC_ISFUZZY(range) ) ) ) {
				/* Only add title if search asks only for artist */
				if( MPC_EQARTIST( range ) ) {
					found=-1;
				}
				/* check for new artist */

				for( i=0; (i<res->anum) && strcmp( res->artists[i], runner->artist ); i++ );

				if( i == res->anum ) {
					res->anum++;
					res->artists=(char**)frealloc( res->artists, res->anum*sizeof(char*) );
					res->artists[i]=runner->artist;
				}
			}
			/* if title or artist matched, also add the album */
			if( ( found && MPC_EQTITLE( range ) ) || ( MPC_ISALBUM(range) && isMatch( runner->album, pat, MPC_ISFUZZY(range) ) ) ) {
				if( MPC_EQALBUM( range ) ) {
					found=-1;
				}

				/*
				 * If an album has more than one artist it will be considered a sampler.
				 * todo: two artists may have an album with the same name!
				 */
				for( i=0; (i<res->lnum) && strcmp( res->albums[i], runner->album ); i++ );

				if( i == res->lnum ) {
					res->lnum++;
					res->albums=(char**)frealloc( res->albums, res->lnum*sizeof(char*) );
					res->albums[i]=runner->album;
					res->albart=(char**)frealloc( res->albart, res->lnum*sizeof(char*) );
					res->albart[i]=runner->artist;
				}
				else if( !strcmp( res->albart[i], ARTIST_SAMPLER ) && strcmp( res->albart[i], runner->artist ) ){
					addMessage(1, "%s is considered a sampler (%s <> %s).", runner->album, runner->artist, res->albart[i] );
					res->albart[i]=ARTIST_SAMPLER;
				}
			}

			if( ( found == -1 ) && ( cnt++ < MAXSEARCH ) ) {
				res->titles=appendToPL( runner, res->titles, 0 );
				res->tnum++;
			}
		}
		runner=runner->next;
	} while( runner != root );

	res->send=-1;

	return (cnt>MAXSEARCH)?-1:cnt;
}

/**
 * applies the dnplist on a list of titles and marks matching titles
 * if th etitle is part of the playlist it will be removed from the playlist
 * too. This may lead to double played artists though...
 */
int applyDNPlist( mptitle *base, struct marklist_t *list ) {
	mptitle  *pos = base;
	struct marklist_t *ptr = list;
	mpplaylist *pl=getConfig()->current;
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
				break;
			}

			ptr=ptr->next;
		}

		pos=pos->next;
	}
	while( pos != base );

	while( pl != NULL ) {
		if( pl->title->flags & MP_DNP ) {
			if( pl == getConfig()->current ) {
				getConfig()->current=pl->next;
			}
			pl=remFromPL(pl);
		}
		else {
			pl=pl->next;
		}
	}

	addMessage( 1, "Marked %i titles as DNP", cnt );

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
				ptr=NULL;
			}
			else {
				ptr=ptr->next;
			}
		}

		runner=runner->next;
	} while ( runner != root );

	addMessage( 1, "Marked %i favourites", cnt );

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

	file=fopen( path, "r" );
	if( !file ) {
		addMessage( 0, "Could not open %s", path );
		return NULL;
	}

	buff=(char*)falloc( MAXPATHLEN, 1 );

	while( !feof( file ) ) {
		fgets( buff, MAXPATHLEN, file );

		if( buff && strlen( buff ) > 1 ) {
			if( !bwlist ) {
				bwlist=(struct marklist_t *)falloc( 1, sizeof( struct marklist_t ) );
				ptr=bwlist;
			}
			else {
				ptr->next=(struct marklist_t *)falloc( 1, sizeof( struct marklist_t ) );
				ptr=ptr->next;
			}

			if( !ptr ) {
				addMessage( 0, "Could not add %s", buff );
				goto cleanup;
			}

			strltcpy( ptr->dir, buff, MAXPATHLEN );
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
void moveEntry( mpplaylist *entry, mpplaylist *pos ) {
	if( pos->next == entry ) {
		return;
	}

	if( pos == entry ) {
		return;
	}

	addToPL( entry->title, pos, 0 );
	remFromPL( entry );
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

	fp=fopen( path, "r" );
	if( !fp ) {
		addMessage( 0, "Could not open playlist %s", path );
		return NULL;
	}

	buff=(char*)falloc( MAXPATHLEN, 1 );
	while( !feof( fp ) ) {
		activity( "Loading" );
		if( ( fgets( buff, MAXPATHLEN, fp ) != NULL ) &&
				( strlen( buff ) > 1 ) && ( buff[0] != '#' ) ) {
			strip( titlePath, buff, MAXPATHLEN ); /* remove control chars like CR/LF */
			current=insertTitle( current, titlePath );
			/* turn list into playlist too */
		}
	}
	free( buff );

	fclose( fp );

	addMessage( 2, "Loaded %s with %i entries.", path, cnt );

	return ( current == NULL )?NULL:current->next;
}

/**
 * Marks an entry as DNP and removes it and additional matching titles
 * from the playlist. Matching is done based on range
 *
 * returns the next item in the list. If the next item is NULL, the previous
 * item will be returned. If entry was the last item in the list NULL will be
 * returned.
 */
mpplaylist *removeByPattern( mpplaylist *plentry, const char *pat ) {
	char pattern[NAMELEN+2];
	mptitle *entry=plentry->title;
	mptitle *runner=entry;

	strncpy( pattern, pat, 2 );

	switch( pattern[0] ){

	case 'l':
		strltcpy( &pattern[2], entry->album, NAMELEN );
		break;

	case 'a':
		strltcpy( &pattern[2], entry->artist, NAMELEN );
		break;

	case 'g':
		strltcpy( &pattern[2], entry->genre, NAMELEN );
		break;

	case 't':
		strltcpy( &pattern[2], entry->title, NAMELEN );
		break;

	case 'p':
		strltcpy( &pattern[2], entry->path, NAMELEN );
		break;

	case 'd':
		strltcpy( &pattern[2], entry->display, NAMELEN );
		break;

	default:
		addMessage( 0, "Unknown pattern %s!", pat );
		addMessage( 0, "Using display instead" );
		strltcpy( &pattern[2], entry->display, NAMELEN );
		break;
	}

	addMessage( 1, "Rule: %s", pattern );
	do {
		if( !(runner->flags & MP_DNP ) && matchTitle( runner, pattern ) ) {
			runner->flags |= MP_DNP;
		}
		runner=runner->next;
	} while( runner != entry );

	return remFromPL(plentry);
}

/**
 * Insert an entry into the database list and fill it with
 * path and if available, mp3 tag info.
 */
mptitle *insertTitle( mptitle *base, const char *path ) {
	mptitle *root;

	root = ( mptitle* ) falloc( 1, sizeof( mptitle ) );

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

	/* remove musicdir from path */
	if( ( getConfig()->musicdir != NULL ) &&
			( strstr( path, getConfig()->musicdir ) != path ) ) {
		strtcpy( root->path, path, MAXPATHLEN );
	}
	else {
		strtcpy( root->path, path+strlen(getConfig()->musicdir), MAXPATHLEN );
	}
	fillTagInfo( root );

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
static unsigned long countTitles( const unsigned int inc, const unsigned int exc ) {
	unsigned long cnt=0;
	mptitle *base=getConfig()->root;
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

		runner=runner->prev;
	}
	while( runner != base );

	return cnt;
}

/**
 * returns the lowest playcount of the current list
 */
unsigned int getLowestPlaycount( ) {
	mptitle *base=getConfig()->root;
	mptitle *runner=base;
	unsigned int min=-1;

	if( base == NULL ) {
		addMessage( 0, "Trying to get lowest playcount from empty database!" );
		return 0;
	}

	do {
		if( !( runner->flags & MP_DNP ) ) {
			if( runner->playcount < min ) {
				min=runner->playcount;
			}
		}
		runner=runner->next;
	}
	while( runner != base );

	return min;
}

/**
 * skips the global list until a title is found that has not been marked
 * and is not marked as DNP
 */
static mptitle *skipOver( mptitle *current, int dir ) {
	mptitle *marker=current;

	while( marker->flags & ( MP_DNP|MP_MARK ) ) {
		if( dir > 0 ) {
			marker=marker->next;
		}
		else {
			marker=marker->prev;
		}

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

		runner=runner->next;
	}
	while( base != runner );

	addMessage( 1, "Marked %i titles as DNP for being skipped", skipskip );
	return skipskip;
}

/**
 * skips the given number of titles
 */
static mptitle *skipTitles( mptitle *current, long num ) {
	if( NULL == current ) {
		return NULL;
	}

	for( ; num > 0; num-- ) {
		current=skipOver(current->next,1);
		if( current == NULL ) {
			addMessage( 0, "Database title ring broken!" );
			return NULL;
		}
	}

	for( ; num < 0; num++ ) {
		current=skipOver(current->next,-1);
		if( current == NULL ) {
			addMessage( 0, "Database title ring broken!" );
			return NULL;
		}
	}

	return current;
}

/**
 * adds a new title to the current playlist
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in a row
 * - prefers titles with lower playcount
 *
 * returns the head of the (new/current) playlist
 */
mpplaylist *addNewTitle( mpplaylist *pl, mptitle *root ) {
	mptitle *runner=NULL;
	mptitle *guard=NULL;
	struct timeval tv;
	unsigned long num=0;
	char *lastpat;
	unsigned int pcount=getLowestPlaycount( );
	unsigned int cycles=0;
	int valid=0;
	/* 0 - nothing checked
	   1 - namecheck okay
	   2 - playcount okay
	   3 - all okay
	  -1 - stuffing
	*/

	if( pl != NULL ) {
		while( pl->next != NULL ) {
			pl=pl->next;
		}
		runner=pl->title;
		lastpat=pl->title->artist ;
	}
	else {
		runner=root;
		valid=3;
	}
	/* improve 'randomization' */
	gettimeofday( &tv,NULL );
	srand( getpid()*tv.tv_sec );

restart:
	num = countTitles( MP_ALL, MP_DNP|MP_MARK );

	/* select a random title from the database */
	/* skip forward until a title is found that is neither DNP nor MARK */
	runner=skipTitles( runner, RANDOM( num ) );

	if( runner==NULL ) {
		if( pl == NULL ) {
			addMessage( 0, "No titles in database and no playlist to add them to.." );
			return NULL;
		}
		runner=pl->title;
		addMessage( 0, "Next round!" );
		newCount( );
		goto restart;
	}

	cycles=0;

	while( ( valid & 3 ) != 3 ) {
		/* title did not pass namecheck */
		if( ( valid & 1 ) != 1 ) {
			guard=runner;

			while( checkSim( runner->artist, lastpat ) ) {
				addMessage( 3, "%s = %s", runner->artist, lastpat );
				activity( "Nameskipping" );
				runner=skipOver( runner->next, 1 );

				if( (runner == guard ) || ( runner == NULL ) ) {
					addMessage( 0, "Only %s left..", lastpat );
					newCount( );
					goto restart;
					break;
				}
			}
			addMessage( 3, "%s != %s", runner->artist, lastpat );

			if( guard != runner ) {
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

			if( ( valid & 2 ) != 2 ) {
				activity( "Playcountskipping" );
				runner=skipOver( runner->next, 1 );
				valid=0;

				if( (runner == NULL ) || ( guard == runner ) ) {
					pcount++;	/* allow more replays */
					addMessage( 2, "Increasing maxplaycount to %li", pcount );
					runner=guard;
				}
			}
		}

		if( ++cycles > 10 ) {
			addMessage( 2, "Looks like we ran into a loop" );
			cycles=0;
			pcount++;	/* allow replays */
			addMessage( 2, "Increasing maxplaycount to %li", pcount );
		}
	} /* while( valid != 3 ) */

	addMessage( 3, "[+] (%i/%li/%3s) %s", runner->playcount, pcount, ONOFF( runner->flags&MP_FAV ), runner->display );

	return appendToPL( runner, pl, -1 );
}

/**
 * checks the current playlist.
 * If there are more than 10 previous titles, those get pruned.
 * While there are less that 10 next titles, titles will be added.
 *
 * todo: make this also work on other playlists
 */
void plCheck( int del ) {
	int cnt=0;
	mpplaylist *pl=getConfig()->current;
	mpplaylist *buf=pl;


	if( ( getConfig()->pledit==1 ) ||
		( ( getConfig()->plplay==1 ) && ( getConfig()->plmix==0 ) ) ){
		return;
	}

	if( pl != NULL ) {

		while( ( pl->prev != NULL ) && ( cnt < 10 ) ) {
			pl=pl->prev;
			cnt++;
		}

		buf=pl->prev;
		pl->prev=NULL;

		while( buf != NULL ) {
			pl=buf->prev;
			free( buf );
			buf=pl;
		}

		cnt=0;
		pl=getConfig()->current;
		/* go to the end of the playlist */
		while( pl->next != NULL ) {
			/* clean up on the way? */
			if( del != 0 ) {
				if ( ( pl->title->flags | MP_DNP ) || ( access( pl->title->path, F_OK ) != 0 ) ) {
					buf=pl->prev;
					pl->prev->next=pl->next;
					if( pl->next != NULL ) {
						pl->next->prev=pl->prev;
					}
					free(pl);
					pl=buf;
					cnt--;
				}
			}
			pl=pl->next;
			cnt++;
		}
	}

	while( cnt < 10 ) {
		if( getConfig()->current == NULL ) {
			getConfig()->current=addNewTitle( NULL, getConfig()->root );
		}
		else {
			addNewTitle( getConfig()->current, getConfig()->root );
		}
		cnt++;
	}
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
 * does a database scan and dumps information about playrate
 * favourites and DNPs
 */
void dumpInfo( mptitle *root, unsigned int skip ) {
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

		if( !( current->flags &MP_DNP ) ) {
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

		current=current->next;
	}
	while( current != root );

	for( pl=minplayed; pl <= maxplayed; pl++ ) {
		unsigned int pcount=0;
		unsigned int dnpcnt=0;
		unsigned int favcnt=0;
		char line[MAXPATHLEN];

		do {
			if( !( current->flags & MP_DNP ) && ( current->playcount == pl ) ) {
				pcount++;
				if( current->flags & MP_DNP ) {
					dnpcnt++;
				}
				if( current->flags & MP_FAV ) {
					favcnt++;
				}
			}
			current=current->next;
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
	addMessage( 0, "%4i\tdo not plays", dnp );
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
 * only favourites are copied
 *
 * TODO this does not make any sense with the new playlist structure
 */
int fillstick( mptitle *root, const char *target ) {
	unsigned int index=0;
	mptitle *current;

	current=root;

	do {
		activity( "Copy title #%03i", index );
		if( current->flags & MP_FAV ) {
			if( copyTitle( current, target, index++ ) == -1 ) {
				break;
			}
		}
		current=current->next;
	}
	while( current != root );

	addMessage( 0, "Copied %i titles to %s", index, target );
	return index;
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
			strtcat( line, title->title, NAMELEN );
			break;
		case mpc_artist:
			strtcat( line, title->artist, NAMELEN );
			break;
		case mpc_album:
			strtcat( line, title->album, NAMELEN );
			break;
		case mpc_genre:
			strtcat( line, title->genre, NAMELEN );
			break;
		case mpc_display:
			strtcat( line, title->display, MAXPATHLEN );
			break;
		default:
			addMessage( 0, "Illegal range 0x%04x", MPC_RANGE(cmd) );
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

/**
 * adds searchresults to the playlist
 * range - title-display/artist/album
 * arg - either a title key or a string
 * insert - play next or append to the end of the playlist
 */
int playResults( mpcmd range, const char *arg, const int insert ) {
	mpconfig   *config=getConfig();
	mpplaylist *pos=config->current;
	mpplaylist *end=config->found->titles;
	mptitle *title=NULL;
	int key=atoi(arg);

	/* insert results at current pos or at the end? */
	if( insert == 0 ) {
		while( pos->next != NULL ) {
			pos=pos->next;
		}
	}

	if( ( range == mpc_title ) || ( range == mpc_display ) ) {
		/* Play the current resultlist */
		if( key == 0 ) {
			/* should not happen but better safe than sorry! */
			if( config->found->tnum == 0 ) {
				addMessage( 0, "No results to be added!" );
				return 0;
			}

			/* find end of searchresults and mark titles as counted on the way */
			end->title->flags|=MP_CNTD;
			while( end->next != NULL ) {
				end=end->next;
				end->title->flags|=MP_CNTD;
			}

			/* make sure the next backlink is set correctly on insert */
			if( pos->next != NULL ) {
				pos->next->prev=end;
			}

			end->next=pos->next;
			pos->next=config->found->titles;
			pos->next->prev=pos;
			return config->found->tnum;
		}

		/* play only one */
		title=getTitleByIndex(key);
		if( title == NULL ) {
			addMessage( 0, "No title with key %i!", key );
			return 0;
		}
		/*
		 * do not count this title as played and do not mark it skipped during this play
		 * this will be reverted after play if needed.
		 */
		title->flags|=MP_CNTD;
		/*
		 * Do not touch marking, we searched the title so it's playing out of order.
		 * It has been played before? Don't care, we want it now and it won't come back!
		 * It's not been played before? Then play it now and whenever it's time comes.
		 */
		addToPL( title, pos, 0 );

		return 1;
	}

	addMessage( 0, "Range not supported!" );
	return 0;
}

/*
 * removes duplicates in the playlist
 */
static int cleanPlaylist( mpplaylist *pl ) {
	mpplaylist *head=pl;
	mpplaylist *runner=NULL;
	int cnt=0;

	if( pl->prev != NULL ) {
		addMessage( 0, "Only cleaning titles to be played!" );
	}

	while( head->next != NULL ) {
		runner=head->next;
		while( runner != NULL ) {
			if( runner->title == head->title ) {
				runner=runner->prev;
				remFromPL( runner->next );
				cnt++;
			}
			runner=runner->next;
		}
		head=head->next;
	}
	return cnt;
}
/*
 * saves the current playlist as m3u
 */
int writePlaylist( mpplaylist *pl, const char *name ) {
	mpconfig *config=getConfig();
	char path[MAXPATHLEN+1];
	char *home=NULL;
	int cnt=0;
	FILE	*fp;

	if( pl == NULL ) {
		addMessage( 0, "Will not write empty playlist!" );
		return -1;
	}

	home=getenv("HOME");
	if( home == NULL ) {
		fail( F_FAIL, "Cannot get HOME!" );
	}

	snprintf( path, MAXPATHLEN, "%s/.mixplay/playlists/%s.m3u", home, name );

	fp=fopen( path, "w" );
	if( fp == NULL ) {
		addMessage( 0, "Could not open playlist file %s", path );
		return -1;
	}

	while( pl->prev != NULL ) {
		pl=pl->prev;
	}

	cnt=cleanPlaylist( pl );
	if( cnt > 0 ) {
		addMessage( 0, "Removed %i duplicate entries" );
		cnt=0;
	}

	fprintf( fp, "#EXTM3U\n" );
	while( pl != NULL ) {
		fprintf( fp, "%s/%s\n", config->musicdir, pl->title->path );
		pl=pl->next;
		cnt++;
	}

	fclose(fp);
	return cnt;
}