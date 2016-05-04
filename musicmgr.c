#include "musicmgr.h"

static struct bwlist_t *blacklist=NULL;
static struct bwlist_t *whitelist=NULL;

/**
 * takes a directory and tries to guess info from the structure
 * Either it's Artist/Album for directories or just the Artist from an mp3
 */
int genPathName( char *name, const char *cd, const size_t len ){
	char *p0, *p1, *p2;
	char curdir[MAXPATHLEN];
	int pl=1;
	int ret=0;

	// Create working copy of the path and cut off trailing /
	strncpy( curdir, cd, MAXPATHLEN );
	if( '/' == curdir[strlen(curdir)] ) {
		curdir[strlen(curdir)-1]=0;
		pl=1; // generating playlist name
	}

	// cut off .mp3
	if( endsWith( curdir, ".mp3" ) ) {
		curdir[strlen( curdir ) - 4]=0;
		pl=0; // guessing artist/title combination
	}

	strcpy( name, "" );

	p0=strrchr( curdir, '/' );
	if( NULL != p0  ) {
		// cut off the last part
		*p0=0;
		p0++;

		p1=strrchr( curdir, '/' );
		if( ( NULL == p1 ) && ( strlen(curdir) < 2 ) ) {
			// No second updir found, so it's just the last dir name
			strncat( name, p0, len  );
		} else {
			if( NULL == p1 ) {
				p1 = curdir;
			} else {
				*p1=0;
				p1++;
			}
			if( pl ) {
				strncat( name, p1, len );
				strncat( name, " - ", len );
				strncat( name, p0, len );
			}
			else {
				p2=strrchr( curdir, '/' );
				if( NULL == p2 ) {
					p2=p1;
				}
				else {
					*p2=0;
					p2++;
				}
				pl=0;
				if( isdigit(p0[0]) ) {
					pl=1;
					if( isdigit(p0[1]) ) {
						pl=2;
					}
				}
				if( ( pl > 0 ) && ( ' ' == p0[pl] ) ) {
					p0=p0+pl+1;
					while(!isalpha(*p0)) {
						p0++;
					}
				}

				ret=strlen(p2);
				strncat( name, p2, len );
				strncat( name, " - ", len );
				strncat( name, p0, len );
			}
		}
	}

	return ret;
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

static int getMusic( const char *cd, struct dirent ***musiclist ){
	return scandir( cd, musiclist, msel, alphasort);
}

int getFiles( const char *cd, struct dirent ***filelist ){
	return scandir( cd, filelist, fsel, alphasort);
}

int getDirs( const char *cd, struct dirent ***dirlist ){
	return scandir( cd, dirlist, dsel, alphasort);
}

/**
 * filters pathnames according to black and whitelist
 */
static int isValid( char *entry ){
	char loname[MAXPATHLEN];
	struct bwlist_t *ptr = NULL;

	strncpy( loname, entry, MAXPATHLEN );
	toLower( loname );

	// Blacklist has priority
	ptr=blacklist;
	while( ptr ){
		if( strstr( loname, ptr->dir ) ) return 0;
		ptr=ptr->next;
	}

	if( whitelist ) {
		ptr=whitelist;
		while( ptr ){
			if( strstr( loname, ptr->dir ) ) return -1;
			ptr=ptr->next;
		}
		return 0;
	}
	else {
		return -1;
	}
}

static int loadBWlist( const char *path, int isbl ){
	FILE *file = NULL;
	struct bwlist_t *ptr = NULL;
	struct bwlist_t *bwlist = NULL;

	char *buff;
	int cnt=0;

	if( isbl ) {
		if( NULL != blacklist )
			fail("Blacklist already loaded! ", path, ENOTRECOVERABLE );
	}
	else {
		if( NULL != whitelist )
			fail("Whitelist already loaded! ", path, ENOTRECOVERABLE );
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

int loadWhitelist( const char *path ){
	return loadBWlist( path, 0 );
}

int addToWhitelist( const char *line ) {
	struct bwlist_t *entry;
	entry=calloc( 1, sizeof( struct bwlist_t ) );
	strncpy( entry->dir, line, MAXPATHLEN );
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
	int cnt;
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
	current=rewindTitles( current );

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

	if( NULL != entry->next ) {
		buff=entry->next;
		buff->prev=entry->prev;

		if( NULL != buff->prev ) {
			buff->prev->next=buff;
		}
	}
	else if( NULL != entry->prev ) {
		buff=entry->prev;
		buff->next=NULL;
	}

	free(entry);
	return buff;
}

/**
 * helperfunction to insert an entry into a list of titles
 */
struct entry_t *insertTitle( struct entry_t *base, const char *path ){
	struct entry_t *root;
	char buff[MAXPATHLEN];
	char *b;

	root = (struct entry_t*) calloc(1, sizeof(struct entry_t));
	if (NULL == root) {
		fail("Malloc failed", "", errno);
	}

	if( NULL != base ) {
		root->next=base->next;
		if( NULL != base->next ) {
			base->next->prev=root;
		}
		base->next=root;
	} else {
		root->next = NULL;
	}
	root->prev=base;

	strcpy( buff, path );
	b = strrchr( buff, '/');
	if (NULL != b) {
		strncpy(root->name, b + 1, MAXPATHLEN);
		b[0] = 0;
		strncpy(root->path, buff, MAXPATHLEN);
	} else {
		strncpy(root->name, buff, MAXPATHLEN);
		strncpy(root->path, "", MAXPATHLEN);
	}
	strncpy(root->display, root->name, MAXPATHLEN );

	return root;
}

int countTitles( struct entry_t *base ) {
	int cnt=0;
	if( NULL == base ){
		return 0;
	}

	base=rewindTitles( base );
	while( NULL != base->next ) {
		cnt++;
		base=base->next;
	}
	if (getVerbosity() > 0 ) printf("Found %i titles\n", cnt );

	return cnt;
}

/**
 * move to the start of the list of titles
 */
struct entry_t *rewindTitles( struct entry_t *base ) {
	// scroll to the end of the list
	while ( NULL != base->next ) base=base->next;
	// scroll back and count entries
	while ( NULL != base->prev ) {
		base=base->prev;
	}
	return base;
}

/**
 * mix a list of titles into a new order
 */
 struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *list=NULL;
	struct entry_t *end=NULL;
	struct entry_t *runner=NULL;
	struct entry_t *guard=NULL;
	char *lastname=NULL;

	int i, num=0, artguard=-1;
	struct timeval tv;

	// improve 'randomization'
	gettimeofday(&tv,NULL);
	srand(getpid()*tv.tv_sec);

	base = rewindTitles( base );
	num  = countTitles(base)+1;

	// Stepping through every item
	while( base != NULL ) {
		int j, pos;
		runner=base;
		pos=RANDOM(num--);
		for(j=1; j<=pos; j++){
			runner=runner->next;
			if( runner == NULL ) {
				runner=base;
			}
		}

		if( artguard && lastname ) {
			guard=runner;
			while( 75 < fncmp( runner->artist, lastname ) ) {
				runner=runner->next;
				if( runner == NULL ) {
					runner=base;
				}
				if( guard == runner ) {
					artguard=0;
					break;
				}
			}
		}

		lastname=runner->artist;

		// Remove entry from base
		if(runner==base) base=runner->next;

		if(runner->prev != NULL){
			runner->prev->next=runner->next;
		}
		if(runner->next != NULL){
			runner->next->prev=runner->prev;
		}

		// add entry to list
		runner->next=NULL;
		runner->prev=end;
		if( NULL != end ) {
			end->next=runner;
		}
		end=runner;

		activity("Shuffling ");
	}
	return rewindTitles( end );
}

struct entry_t *skipTitles( struct entry_t *current, int num, int repeat, int mix ) {
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
			if( NULL != current->prev ) {
				current=current->prev;
			}
			else {
				num=1;
			}
		}
		else {
			if( NULL != current->next ) {
				current=current->next;
			}
			else {
				if( repeat ) {
					if( mix ) {
						current=shuffleTitles( current );
					}
					else {
						current=rewindTitles( current );
					}
				}
				else {
					num=1;
				}
			}

		}
		num--;
	}

	return current;
}

int checkWhitelist( struct entry_t *root ) {
	char loname[MAXPATHLEN];
	struct bwlist_t *ptr = NULL;
	struct entry_t *runner=root;

	if(!whitelist) return -1;

	while ( NULL != runner) {
		sprintf( loname, "%s/%s", runner->path, runner->name );
		toLower( loname );
		runner->rating=0;
		ptr=whitelist;
		while( ptr ){
			if( strstr( loname, ptr->dir ) ){
				runner->rating=1;
				break;
			}
			ptr=ptr->next;
		}
		runner=runner->next;
		activity("Favourites ");
	}
	return 0;
}


/*
 * Steps recursively through a directory and collects all music files in a list
 * curdir: current directory path
 * files:  the list to store filenames in
 */
struct entry_t *recurse( char *curdir, struct entry_t *files ) {
	struct entry_t *buff=NULL;
	char dirbuff[MAXPATHLEN];
	DIR *directory;
	FILE *file;
	struct dirent **entry;
	int num, i;
	char *pos;

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
		if( '/' != dirbuff[strlen(dirbuff)] ) {
			strncat( dirbuff, "/", MAXPATHLEN );
		}
		strncat( dirbuff, entry[i]->d_name, MAXPATHLEN );

		if( isValid(dirbuff) ) {
			int alen;
			file=fopen( dirbuff, "r");
			if( NULL == file ) fail("Couldn't open file ", dirbuff,  errno);
			if( -1 == fseek( file, 0L, SEEK_END ) ) fail( "fseek() failed on ", dirbuff, errno );

			buff=(struct entry_t *)calloc(1, sizeof(struct entry_t));
			if(buff == NULL) fail("Out of memory!", "", errno);

			strncpy( buff->name, entry[i]->d_name, MAXPATHLEN );
			alen=genPathName( buff->display, dirbuff, MAXPATHLEN );
	//		strncpy( buff->title, entry[i]->d_name, MAXPATHLEN );
			strncpy( buff->path, curdir, MAXPATHLEN );
			strncpy( buff->artist, buff->display, alen );
			buff->artist[alen+1]=0;
			buff->prev=files;
			buff->next=NULL;
			if(files != NULL)files->next=buff;
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
//		activity("Scanning");
		sprintf( dirbuff, "%s/%s", curdir, entry[i]->d_name );
		files=recurse( dirbuff, files );
		free(entry[i]);
	}
	free(entry);

	return files;
}
