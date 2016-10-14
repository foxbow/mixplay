#ifndef MUSICMGR_H_
#define MUSICMGR_H_

#include <dirent.h>

/* Directory access */

#define NAMELEN 64
#define MAXPATHLEN 256

#define RANDOM(x) (rand()%x)


#define MP_FAV  1	// Favourite
#define MP_DNP  2    // do not play
#define MP_CNTD 4	// has been counted

#define SL_TITLE 1
#define SL_ALBUM 2
#define SL_ARTIST 4
#define SL_GENRE 8
#define SL_PATH 16

struct entry_t {
	char path[MAXPATHLEN];		// path on the filesystem to the file
	char artist[NAMELEN];		// Artist info
	char title[NAMELEN];		// Title info (from mp3)
	char album[NAMELEN];		// Album info (from mp3)
	unsigned int played;		// play counter
	char genre[NAMELEN];
	struct entry_t *prev;		//
	unsigned int key;			// DB key/index  - internal
	char display[MAXPATHLEN];	// Title display - internal
	unsigned int flags;			// 1=favourite   - internal
	struct entry_t *next;
};

struct bwlist_t {
	char dir[MAXPATHLEN];
	struct bwlist_t *next;
};

/**
 * Music helper functions
 */
void wipeTitles( struct entry_t *files );
struct entry_t *recurse( char *curdir, struct entry_t *files, const char *basedir );
struct entry_t *shuffleTitles( struct entry_t *base );
struct entry_t *rewindTitles( struct entry_t *base );
struct entry_t *removeTitle( struct entry_t *entry );
struct entry_t *loadPlaylist( const char *path );
struct entry_t *insertTitle( struct entry_t *base, const char *path );
struct entry_t *skipTitles( struct entry_t *current, int num );
struct entry_t *useDNPlist( struct entry_t *base, struct bwlist_t *list );
struct entry_t *searchList( struct entry_t *base, struct bwlist_t *term, int range );
void moveEntry( struct entry_t *entry, struct entry_t *pos );

int countTitles( struct entry_t *base );
unsigned long getLowestPlaycount( struct entry_t *base );
struct bwlist_t *loadList( const char *path );
int isMusic( const char *name );
struct bwlist_t *addToList( const char *line, struct bwlist_t **list );
int applyFavourites( struct entry_t *root, struct bwlist_t *list );
int mp3Exists( const struct entry_t *title );
int getFiles( const char *cd, struct dirent ***filelist );
int getDirs( const char *cd, struct dirent ***dirlist );
struct entry_t *findTitle( struct entry_t *base, const char *path );
char *getGenre( struct entry_t *title );
void dumpTitles( struct entry_t *root );

#endif /* MUSICMGR_H_ */
