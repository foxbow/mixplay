#ifndef MUSICMGR_H_
#define MUSICMGR_H_

#include <dirent.h>

/* Directory access */

#define NAMELEN 64
#define MAXPATHLEN 256

#define RANDOM(x) (abs(rand()%x))


#define MP_FAV  1	// Favourite
#define MP_DNP  2   // do not play
#define MP_CNTD 4	// has been counted
#define MP_SKPD 8   // has been skipped
#define MP_MARK 16	// has been marked
#define MP_ALL  31

#define SL_UNSET   0
#define SL_TITLE   1
#define SL_ALBUM   2
#define SL_ARTIST  4
#define SL_GENRE   8
#define SL_PATH   16
#define SL_DISPLAY 32

struct entry_t {
	char path[MAXPATHLEN];		// path on the filesystem to the file
	char artist[NAMELEN];		// Artist info
	char title[NAMELEN];		// Title info (from mp3)
	char album[NAMELEN];		// Album info (from mp3)
	unsigned int played;		// play counter
	unsigned int skipped;		// skip counter
	char genre[NAMELEN];
	struct entry_t *plprev;		// playlist pointer
	unsigned int key;			// DB key/index  - internal
	char display[MAXPATHLEN];	// Title display - internal
	unsigned int flags;			// 1=favourite   - internal
	struct entry_t *plnext;
	struct entry_t *dbprev;		// database pointer
	struct entry_t *dbnext;
};

struct marklist_t {
	char dir[MAXPATHLEN];
	struct marklist_t *next;
};

/**
 * Music helper functions
 */
void addToFile( const char *path, const char *line, const char* prefix );

struct entry_t *recurse( char *curdir, struct entry_t *files, const char *basedir );
struct entry_t *shuffleTitles( struct entry_t *base );
struct entry_t *rewindTitles( struct entry_t *base );
struct entry_t *removeFromPL( struct entry_t *current, const unsigned int range );
struct entry_t *loadPlaylist( const char *path );
struct entry_t *insertTitle( struct entry_t *base, const char *path );
struct entry_t *skipTitles( struct entry_t *current, int num, const int global );
struct entry_t *searchList( struct entry_t *base, struct marklist_t *term );
int DNPSkip( struct entry_t *base, const unsigned int level );
int applyDNPlist( struct entry_t *base, struct marklist_t *list );
int applyFavourites( struct entry_t *root, struct marklist_t *list );
int markFavourite( struct entry_t *title, int range );

struct entry_t *cleanTitles( struct entry_t *root );
struct marklist_t *cleanList( struct marklist_t *root );

void moveEntry( struct entry_t *entry, struct entry_t *pos );
void newCount( struct entry_t * root);
unsigned int getLowestPlaycount( struct entry_t *base, const int global );
struct marklist_t *loadList( const char *path );
int isMusic( const char *name );
struct marklist_t *addToList( const char *line, struct marklist_t **list );
void dumpTitles( struct entry_t *root, const int pl );
void dumpInfo( struct entry_t *root, int db );
#endif /* MUSICMGR_H_ */
