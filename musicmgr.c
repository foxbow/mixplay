#include "musicmgr.h"

static struct bwlist_t *blacklist=NULL;
static struct bwlist_t *whitelist=NULL;

/**
 * moves *title from the original list and inserts it after *target
 */
static struct entry_t *moveTitle( struct entry_t *title, struct entry_t **target ) {
	// remove title from old list
	title->prev->next=title->next;
	title->next->prev=title->prev;

	if( NULL == target[0] ) {
		target[0]=title;
		title->next=title;
		title->prev=title;
	}
	else {
		title->next=target[0]->next;
		title->next->prev=title;
		title->prev=target[0];
		target[0]->next=title;
	}

	return title;
}

/**
 * takes a directory and tries to guess info from the structure
 * Either it's Artist/Album for directories or just the Artist from an mp3
 */
int genPathName( const char *basedir, struct entry_t *entry  ){
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

	strcpy( entry->artist, "Various" );
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

	snprintf( entry->display, MAXPATHLEN, "%s - %s", entry->artist, entry->title );
	return strlen( entry->display );
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
	v1= atoi( d1[0]->d_name );
	v2= atoi( d2[0]->d_name );
	if( ( v1 > 0 ) && ( v2 > 0 ) ) return( v1-v2 );
	return strcasecmp( d1[0]->d_name, d2[0]->d_name );
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

struct entry_t *useWhitelist( struct entry_t *base ) {
	struct entry_t  *runner=base;
	struct entry_t  *next=NULL;
	struct entry_t  *result=NULL;
	struct bwlist_t *term=NULL;
	char   loname[MAXPATHLEN];
	int len;
	int trigger;

	if( NULL == base ) {
		fail("No music loaded!", "Nothing to search in", F_FAIL );
	}

	term=whitelist;

	while( term != NULL ) {
		toLower(term->dir);
		do{
			strcpy( loname, runner->path );
			len=MIN(strlen(loname), strlen(term->dir) );
			trigger=70;
			if( len <= 20 ) trigger=80;
			if( len <= 10 ) trigger=88;
			if( len <= 5 ) trigger=100;
			if( trigger <= fncmp( loname, term->dir ) ){
				next=runner->next;
				if( runner==base ) {
					base=next;
					next=next->next; // @todo this may skip a title..
				}

				result=moveTitle( runner, &result );
				runner=next;
			}
			else {
				runner=runner->next;
			}
		} while( runner != base );
		term=term->next;
	}

	wipeTitles( base );

	return result?result->next:NULL;
}

/**
 * applies the blacklist on a list of titles and removes matching titles
 * from the list
 */
struct entry_t *useBlacklist( struct entry_t *base ) {
	struct entry_t *pos=base;
	struct bwlist_t *ptr = NULL;
	char loname[512];

	if( NULL == base ) {
		fail("No music loaded!", "No blacklist used", F_FAIL );
	}

	if( NULL == blacklist ) {
		return base;
	}

	do{
		strncpy( loname, pos->display, 512 );
		strncat( loname, pos->path, 512-strlen(loname) );
		toLower( loname );

		ptr=blacklist;
		while( ptr ){
			if( strstr( ptr->dir, loname ) ) {
				if( pos == base ) base=base->next;
				pos=removeTitle(pos);
				break;
			}
			ptr=ptr->next;
		}

		if( pos == NULL ) {
			fail("List emptied!", "No more titles..", F_FAIL );
		}

		pos=pos->next;
	} while( pos != base );

	return base;
}

/**
 * filters pathnames according to black and whitelist
 * this does a fuzzy matching against the whitelist
 */
static int isValid( char *entry ){
	char loname[MAXPATHLEN];
	struct bwlist_t *ptr = NULL;
	int len=0, pos=0;
	int trigger=0;

	strncpy( loname, entry, MAXPATHLEN );
	toLower( loname );

	for( len=strlen(entry); len>0; len-- ) {
		if(entry[len]=='/') pos++;
		if( pos == 3 ){
			strcpy( loname, &entry[len] );
			break;
		}
	}
	if( len == 0 ) strcpy( loname, entry );

	// Blacklist has priority
	ptr=blacklist;
	while( ptr ){
		if( strstr( loname, ptr->dir ) ) return 0;
		ptr=ptr->next;
	}

	if( whitelist ) {
		ptr=whitelist;
		while( ptr ){
			len=MIN(strlen(entry), strlen(ptr->dir) );
			trigger=70;
			if( len <= 20 ) trigger=80;
			if( len <= 10 ) trigger=88;
			if( len <= 5 ) trigger=100;
			strcpy( loname, entry );
			if( trigger <= fncmp( toLower(loname), ptr->dir ) ){
				return -1;
			}
			ptr=ptr->next;
		}
		return 0;
	}
	else {
		return -1;
	}
}

/**
 * does the actual loading of a blacklist or favourites list
 */
static int loadBWlist( const char *path, int isbl ){
	FILE *file = NULL;
	struct bwlist_t *ptr = NULL;
	struct bwlist_t *bwlist = NULL;

	char *buff;
	int cnt=0;

	if( isbl ) {
		if( NULL != blacklist )
			fail("Blacklist already loaded! ", path, -1 );
	}
	else {
		if( NULL != whitelist )
			// fail("Whitelist already loaded! ", path, -1 );
			return 0;
	}

	buff=calloc( MAXPATHLEN, sizeof(char) );
	if( !buff ) fail( "Out of memory", "", errno );

	file=fopen( path, "r" );
	if( !file ) return 0;
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
			if( !ptr ) fail( "Out of memory!", "", errno );
			strncpy( ptr->dir, toLower(buff), strlen( buff ) );
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

	if( isbl ) {
		blacklist=bwlist;
	}
	else {
		whitelist=bwlist;
	}

	return cnt;
}

/**
 * load a blacklist file into the blacklist structure
 */
int loadBlacklist( const char *path ){
	return loadBWlist( path, 1 );
}

/**
 * load a favourites file into the whitelist structure
 */
int loadWhitelist( const char *path ){
	return loadBWlist( path, 0 );
}

/**
 * add an entry to the whitelist
 */
int addToWhitelist( const char *line ) {
	struct bwlist_t *entry;
	entry=calloc( 1, sizeof( struct bwlist_t ) );
	strncpy( entry->dir, line, MAXPATHLEN );
	toLower( entry->dir );
	if( !whitelist ) {
		whitelist=entry;
		entry->next=NULL;
	}
	else {
		entry->next=whitelist->next;
		whitelist=entry;
	}
	return 0;
}

/**
 * load a standard m3u playlist into a list of titles that the tools can handle
 */
struct entry_t *loadPlaylist( const char *path ) {
	FILE *fp;
	int cnt=0;
	struct entry_t *current=NULL;
	char *buff;

	buff=calloc( MAXPATHLEN, sizeof(char) );
	if( !buff ) fail( "Out of memory", "", errno );

	fp=fopen( path, "r" );
	if( !fp ) fail("Couldn't open playlist ", path,  errno);

	while( !feof( fp ) ){
		activity("Loading");
		buff=fgets( buff, MAXPATHLEN, fp );
		if( buff && ( strlen( buff ) > 1 ) && ( buff[0] != '#' ) ){
			current=insertTitle( current, buff );
		}
	}
	fclose( fp );

	if( getVerbosity() > 2 ) {
		printf("Loaded %s with %i entries.\n", path, cnt );
	}

	return current;
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
		buff=files->next;
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
		fail("Malloc failed", "", errno);
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
	genPathName( "", root );

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

//	base=rewindTitles( base );
	do {
		cnt++;
		runner=runner->next;
	} while( runner != base );
	if (getVerbosity() > 0 ) printf("Found %i titles\n", cnt );

	return cnt;
}

static unsigned long getLowestPlaycount( struct entry_t *base ) {
	struct entry_t *runner=base;
	unsigned long min=-1;

	do {
		if( runner->played < min ) min=base->played;
		runner=runner->next;
	} while( runner != base );

	return min;
}

/**
 * mix a list of titles into a random order
 * @todo: this sometimes ends up with a messed up list
 */
 struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *end=NULL;
	struct entry_t *runner=NULL;
	struct entry_t *guard=NULL;
	char *lastname=NULL;
	int num=0, artguard=-1;
	struct timeval tv;
	unsigned long count=0;

	// improve 'randomization'
	gettimeofday(&tv,NULL);
	srand(getpid()*tv.tv_sec);

	count=getLowestPlaycount( base );
	num = countTitles(base);

	// Stepping through every item
	while( base->next != base ) {
		int pos;
		activity("Shuffling ");

		// select a title at random
		pos=RANDOM(num);
		runner=skipTitles( base, pos );

		// check for duplicates
		if( artguard && lastname ) {
			guard=runner;
			while( 80 < fncmp( runner->artist, lastname ) ) {
				runner=runner->next;
				if( guard == runner ) {
					artguard=0;
					break;
				}
			}
		}

		// check for playcount
		guard=runner;
		do {
			if( runner->played < count ) break;
			runner=runner->next;
		} while( runner != guard );

		if( runner == guard ) {
			count=runner->played;
		}

		lastname=runner->artist;

		// Make sure we stay in the right list
		if(runner == base) {
			base=runner->next;
		}

		end = moveTitle( runner, &end );
	}

	// add the last title
	end=moveTitle( base, &end );

	return end->next;
}

/**
 * skips the given number of titles
 * this also takes 'repeat' and 'mix' into account.
 * Caveat: you cannot skip before the first title
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
 * applies the loaded whitelist against the list of entries
 * This function uses a literal comparison to identify true
 * favourites and is not supposed to use a fuzzy search.
 */
int checkWhitelist( struct entry_t *root ) {
	char loname[MAXPATHLEN];
	struct bwlist_t *ptr = NULL;
	struct entry_t *runner=root;

	if(!whitelist) return -1;

	do {
		activity("Favourites ");
		strcpy( loname, runner->path );
		toLower( loname );
//		runner->flags|=MP_FAV;
		ptr=whitelist;
		while( ptr ){
			if( strstr( loname, ptr->dir ) ){
				runner->flags|=MP_FAV;
				break;
			}
			ptr=ptr->next;
		}
		runner=runner->next;
	} while ( runner != root );

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
	FILE *file;
	struct dirent **entry;
	int num, i;

	if( '/' == curdir[strlen(curdir)-1] ){
		curdir[strlen(curdir)-1]=0;
	}

	if( getVerbosity() > 2 ) {
		printf("Checking %s\n", curdir );
	}
	// get all music files according to the blacklist
	num = getMusic( curdir, &entry );
	if( num < 0 ) {
		fail( "getMusic failed", curdir, errno );
	}
	for( i=0; i<num; i++ ) {
		activity("Scanning");
		strncpy( dirbuff, curdir, MAXPATHLEN );
		if( '/' != dirbuff[strlen(dirbuff)-1] ) {
			strncat( dirbuff, "/", MAXPATHLEN );
		}
		strncat( dirbuff, entry[i]->d_name, MAXPATHLEN );

		if( isValid(dirbuff) ) {
			file=fopen( dirbuff, "r");
			if( NULL == file ) fail("Couldn't open file ", dirbuff,  errno);
			if( -1 == fseek( file, 0L, SEEK_END ) ) fail( "fseek() failed on ", dirbuff, errno );

			buff=(struct entry_t *)calloc(1, sizeof(struct entry_t));
			if(buff == NULL) fail("Out of memory!", "", errno);

			strncpy( buff->path, dirbuff, MAXPATHLEN );
			genPathName( basedir, buff );
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

			buff->size=ftell( file )/1024;
			files=buff;
			fclose(file);
		}
		free(entry[i]);
	}
	free(entry);

	num=getDirs( curdir, &entry );
	if( num < 0 ) {
		fail( "getDirs", curdir, errno );
	}
	for( i=0; i<num; i++ ) {
		sprintf( dirbuff, "%s/%s", curdir, entry[i]->d_name );
		files=recurse( dirbuff, files, basedir );
		free(entry[i]);
	}
	free(entry);

	return files;
}

#ifdef DEBUG
/**
 * just for debugging purposes!
 */
void dumpTitles( struct entry_t *root, char *msg ) {
	struct entry_t *ptr=root;
	if( NULL==root ) fail("NO LIST", msg, F_FAIL );
	do {
		puts( ptr->path );
		ptr=ptr->next;
	} while( ptr != root );
	fail("END DUMP",msg,F_FAIL );
}
#endif
