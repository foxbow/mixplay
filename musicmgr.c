#include "musicmgr.h"

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
 */
static struct entry_t *moveTitle( struct entry_t *title, struct entry_t **target ) {
	// remove title from old list
	title->prev->next=title->next;
	title->next->prev=title->prev;

	if( NULL == target[0] ) {
		target[0]=title;
		target[0]->next=target[0];
		target[0]->prev=target[0];
	}
	else {
		title->next=target[0]->next;
		target[0]->next->prev=title;
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

static int checkMatch( const char* name, const char* pat ) {
	int len;
	char loname[MAXPATHLEN];
	int trigger;

	len=MIN(strlen(name), strlen(pat) );
	trigger=70;
	if( len <= 20 ) trigger=80;
	if( len <= 10 ) trigger=88;
	if( len <= 5 ) trigger=100;
	strncpy( loname, name, MAXPATHLEN );
	if( trigger <= fncmp( toLower(loname), pat ) ){
		return -1;
	}
	return 0;
}


int matchList( struct entry_t **result, struct entry_t *base, struct bwlist_t *term, int range ) {
	struct entry_t  *runner=base;
	struct entry_t  *next=NULL;
	int match=0;
	int cnt;

	char *mdesc[]={
			"title"
			,"album"
			,"artist"
			,"genre"
			,"path"
	};

	if( NULL == base ) {
		fail("No music loaded!", "Nothing to search in", F_FAIL );
	}

	while( term != NULL ) {
		while( runner->next != base ){
			switch( range ) {
				case SL_TITLE:
					match=checkMatch( runner->title, term->dir );
				break;
				case SL_ALBUM:
					match=checkMatch( runner->album, term->dir );
				break;
				case SL_ARTIST:
					match=checkMatch( runner->artist, term->dir );
				break;
				case SL_GENRE:
					match=checkMatch( getGenre(runner), term->dir );
				break;
				case SL_PATH:
					match=checkMatch( runner->path, term->dir );
				break;
				default:
					fail("invalid checkMatch call", "", range );
			}
			if( match ) {
				next=runner->next;
				if( runner==base ) {
					base=base->next;       // make sure base stays valid after removal
				}

				moveTitle( runner, result );
				cnt++;
				runner=next;
			}
			else {
				runner=runner->next;
			}
		} //  while( runner != base );
		term=term->next;
	}

//	wipeTitles( base );

	if( getVerbosity() ) printf("Added %i titles by %s \n", cnt, mdesc[range] );

	return cnt;
}

struct entry_t *searchList( struct entry_t *base, struct bwlist_t *term, int range ) {
	struct entry_t  *result=NULL;
	int cnt=0;

	if( NULL == base ) {
		fail("No music loaded!", "Nothing to search in", F_FAIL );
	}

	if( range & SL_ARTIST ) cnt += matchList( &result, base, term, SL_ARTIST );
	if( range & SL_TITLE ) cnt += matchList( &result, base, term, SL_TITLE );
	if( range & SL_ALBUM ) cnt += matchList( &result, base, term, SL_ALBUM );
	if( range & SL_PATH ) cnt += matchList( &result, base, term, SL_PATH );
	if( range & SL_GENRE ) cnt += matchList( &result, base, term, SL_GENRE );

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
		fail("No music loaded!", "No list used", F_FAIL );
	}

	if( NULL == list ) {
		return base;
	}

	do{
		strncpy( loname, pos->path, NAMELEN+MAXPATHLEN );
		strncat( loname, pos->display, NAMELEN+MAXPATHLEN-strlen(loname) );
		toLower( loname );

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
			fail("List emptied!", "No more titles..", F_FAIL );
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
	if( !buff ) fail( "Out of memory", "", errno );

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
			if( !ptr ) fail( "Out of memory!", "", errno );
			strncpy( ptr->dir, toLower(buff), MAXPATHLEN );
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
 * add an entry to a list
 */
struct bwlist_t *addToList( const char *line, struct bwlist_t *list ) {
	struct bwlist_t *entry;
	entry=calloc( 1, sizeof( struct bwlist_t ) );
	strncpy( entry->dir, line, MAXPATHLEN );
	toLower( entry->dir );
	entry->next = NULL;
	if( NULL == list ) {
		list=entry;
	}
	else {
		list->next=entry;
	}

	return list;
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

	do {
		cnt++;
		runner=runner->prev;
	} while( runner != base );
	if (getVerbosity() > 1 ) printf("Found %i titles\n", cnt );

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
 */
 struct entry_t *shuffleTitles( struct entry_t *base ) {
	struct entry_t *end=NULL;
	struct entry_t *runner=NULL;
	struct entry_t *guard=NULL;
	char *lastname=NULL;
	int num=0, artguard=-1;
	int nameskip=0;
	int playskip=0;
	struct timeval tv;
	unsigned long count=0;

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

		// check for duplicates
		if( artguard && lastname ) {
			guard=runner;
			while( 75 < fncmp( runner->artist, lastname ) ) {
				runner=runner->next;
				if( guard == runner ) {
					artguard=0;
					break;
				}
			}
			if( guard != runner ) nameskip++;
		}

		// check for playcount
		guard=runner;
		if( !(runner->flags & MP_FAV) ) { // favourites override playcount
			do {
				if( runner->played <= count ) break;
				runner=runner->next;
			} while( runner != guard );
		}

		if( runner != guard ) {
			playskip++;
		}
		else {
			count=runner->played;
		}

		lastname=runner->artist;

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
		strcpy( loname, runner->path );
		toLower( loname );
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
	FILE *file;
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
		fail( "getMusic failed", curdir, errno );
	}
	for( i=0; i<num; i++ ) {
		activity("Scanning");
		strncpy( dirbuff, curdir, MAXPATHLEN );
		if( '/' != dirbuff[strlen(dirbuff)-1] ) {
			strncat( dirbuff, "/", MAXPATHLEN );
		}
		strncat( dirbuff, entry[i]->d_name, MAXPATHLEN );

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
	if( NULL==root ) fail("NO LIST", "", F_FAIL );
	do {
		printf("[%04li] %s: %s - %s (%s)\n", ptr->key, ptr->path, ptr->artist, ptr->title, ptr->album );
		ptr=ptr->next;
	} while( ptr != root );
	fail("END DUMP","",F_FAIL );
}
