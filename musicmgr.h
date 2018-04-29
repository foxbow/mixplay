#ifndef _MUSICMGR_H_
#define _MUSICMGR_H_

#include <dirent.h>

/* Directory access */

#define NAMELEN 64
#define MAXPATHLEN 256

#define RANDOM(x) (abs(rand()%x))

#define MP_FAV  1	/* Favourite */
#define MP_DNP  2   /* do not play */
#define MP_CNTD 4	/* has been counted */
#define MP_SKPD 8   /* has been skipped */
#define MP_MARK 16	/* has been marked */
#define MP_ALL  31

typedef struct mptitle_t mptitle;
struct mptitle_t {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	unsigned int playcount;		/* play counter */
	unsigned int skipcount;		/* skip counter */
	char genre[NAMELEN];
	mptitle *plprev;			/* playlist pointer */
	unsigned int key;			/* DB key/index  - internal */
	char display[MAXPATHLEN];	/* Title display - internal */
	unsigned int flags;			/* 1=favourite   - internal */
	mptitle *plnext;
	mptitle *dbprev;			/* database pointer */
	mptitle *dbnext;
};

struct marklist_t {
	char dir[MAXPATHLEN+2];
	struct marklist_t *next;
};

#include "config.h"

/**
 * Music helper functions
 */
void addToFile( const char *path, const char *line );

mptitle *addToPL( mptitle *title, mptitle *target );
mptitle *recurse( char *curdir, mptitle *files );
mptitle *shuffleTitles( mptitle *base );
mptitle *rewindTitles( mptitle *base );
mptitle *loadPlaylist( const char *path );
mptitle *insertTitle( mptitle *base, const char *path );
mptitle *skipTitles( mptitle *current, int num, const int global );
mptitle *searchList( mptitle *base, struct marklist_t *term );
mptitle *getTitle( unsigned int key );
mptitle *cleanTitles( mptitle *root );

int DNPSkip( mptitle *base, const unsigned int level );
int applyDNPlist( mptitle *base, struct marklist_t *list );
int applyFAVlist( mptitle *root, struct marklist_t *list );
int searchPlay( mptitle *root, const char *pat, unsigned num, const int global );
int handleRangeCmd( mptitle *title, mpcmd cmd );
int addRangePrefix( char *line, mpcmd cmd );

struct marklist_t *cleanList( struct marklist_t *root );
struct marklist_t *loadList( const char *path );
struct marklist_t *addToList( const char *line, struct marklist_t **list );

void moveEntry( mptitle *entry, mptitle *pos );
void newCount( mptitle * root );
unsigned int getLowestPlaycount( mptitle *base, const int global );
int isMusic( const char *name );
void dumpTitles( mptitle *root, const int pl );
void dumpInfo( mptitle *root, int db, int skip );
int fillstick( mptitle *root, const char *target, int fav );
int writePlaylist( mptitle *root, const char *path );

#endif /* _MUSICMGR_H_ */
