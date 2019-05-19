#ifndef _MUSICMGR_H_
#define _MUSICMGR_H_

#include <dirent.h>

/* Directory access */

#define NAMELEN 64
#define MAXPATHLEN 256
#define MAXSEARCH 100

#define MP_FAV  1	/* Favourite */
#define MP_DNP  2	/* do not play */
#define MP_CNTD 4	/* has been counted */
#define MP_MARK 8	/* is currently in the playlist */
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
	unsigned int key;			/* DB key/index  - internal */
	char display[MAXPATHLEN];	/* Title display - internal */
	unsigned int flags;			/* 1=favourite   - internal */
	mptitle *prev;				/* database pointer */
	mptitle *next;
};

typedef struct playlist_t mpplaylist;
struct playlist_t {
	mptitle	*title;
	mpplaylist *prev;
	mpplaylist *next;
};

typedef struct searchresults_t searchresults;
struct searchresults_t {
	unsigned tnum;
	unsigned anum;
	unsigned lnum;
	mpplaylist *titles;
	char **artists;
	char **albums;
	char **albart;
	int send;
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

/**
 * playlist functions
 */
mpplaylist *appendToPL( mptitle *title, mpplaylist *pl, const int mark );
mpplaylist *addToPL( mptitle *title, mpplaylist *target, const int mark );
mpplaylist *remFromPLByKey( mpplaylist *root, const unsigned key );
void moveEntry( mpplaylist *entry, mpplaylist *pos );
mpplaylist *wipePlaylist( mpplaylist *pl );
mpplaylist *addPLDummy( mpplaylist *pl, const char *name );
void plCheck( int del );
int writePlaylist( mpplaylist *pl, const char *name );

mptitle *recurse( char *curdir, mptitle *files );
mptitle *rewindTitles( mptitle *base );
mptitle *loadPlaylist( const char *path );
mptitle *insertTitle( mptitle *base, const char *path );
mptitle *wipeTitles( mptitle *root );
int search( const char *pat, const mpcmd range, const int global );
int playResults( mpcmd range, const char *arg, const int insert );

int DNPSkip( mptitle *base, const unsigned int level );
int applyDNPlist( mptitle *base, struct marklist_t *list );
int applyFAVlist( mptitle *root, struct marklist_t *list, int excl );
int searchPlay( const char *pat, unsigned num, const int global );
int handleRangeCmd( mptitle *title, mpcmd cmd );
int addRangePrefix( char *line, mpcmd cmd );

struct marklist_t *cleanList( struct marklist_t *root );
struct marklist_t *loadList( const char *path );
struct marklist_t *addToList( const char *line, struct marklist_t **list );

void newCount( void );
int isMusic( const char *name );
void dumpTitles( mptitle *root, const int pl );
void dumpInfo( mptitle *root, unsigned int skip );
int fillstick( mptitle *root, const char *target );
int getPlaylists( const char *cd, struct dirent ***pllist );

unsigned getLowestPlaycount( void );
#endif /* _MUSICMGR_H_ */
