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

typedef struct mptitle_s mptitle_t ;
struct mptitle_s {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	unsigned playcount;		/* play counter */
	unsigned skipcount;		/* skip counter */
	char genre[NAMELEN];
	unsigned key;			/* DB key/index  - internal */
	char display[MAXPATHLEN];	/* Title display - internal */
	unsigned flags;			/* 1=favourite   - internal */
	mptitle_t *prev;				/* database pointer */
	mptitle_t *next;
};

typedef struct playlist_t mpplaylist;
struct playlist_t {
	mptitle_t  *title;
	mpplaylist *prev;
	mpplaylist *next;
};

typedef struct {
	unsigned tnum;
	unsigned anum;
	unsigned lnum;
	mpplaylist *titles;
	char **artists;
	char **albums;
	char **albart;
	unsigned send:1;
} searchresults;

typedef struct marklist_s marklist_t;
struct marklist_s {
	char dir[MAXPATHLEN+2];
	marklist_t *next;
};

#include "config.h"

/**
 * playlist functions
 */
mpplaylist *appendToPL( mptitle_t *title, mpplaylist *pl, const int mark );
mpplaylist *addToPL( mptitle_t *title, mpplaylist *target, const int mark );
mpplaylist *remFromPLByKey( mpplaylist *root, const unsigned key );
void moveEntry( mpplaylist *entry, mpplaylist *pos );
mpplaylist *wipePlaylist( mpplaylist *pl );
mpplaylist *addPLDummy( mpplaylist *pl, const char *name );
void plCheck( int del );
int writePlaylist( mpplaylist *pl, const char *name );

mptitle_t *recurse( char *curdir, mptitle_t *files );
mptitle_t *rewindTitles( mptitle_t *base );
mptitle_t *loadPlaylist( const char *path );
mptitle_t *insertTitle( mptitle_t *base, const char *path );
mptitle_t *wipeTitles( mptitle_t *root );
int search( const char *pat, const mpcmd_t range );
int playResults( mpcmd_t range, const char *arg, const int insert );

void markSkip( mptitle_t *title );
int DNPSkip( void );
void applyLists( int clean );
int searchPlay( const char *pat, unsigned num, const int global );
int handleRangeCmd( mptitle_t *title, mpcmd_t cmd );
int addRangePrefix( char *line, mpcmd_t cmd );

marklist_t *wipeList( marklist_t *root );
marklist_t *loadList( const mpcmd_t cmd );
int delFromList( const mpcmd_t cmp, const char *line );
int writeList( const mpcmd_t cmd );

void newCount( void );
int isMusic( const char *name );
void dumpTitles( mptitle_t *root, const int pl );
void dumpInfo( mptitle_t *root, unsigned int skip );
int fillstick( mptitle_t *root, const char *target );
int getPlaylists( const char *cd, struct dirent ***pllist );
unsigned long countTitles( const unsigned int inc, const unsigned int exc );
unsigned getLowestPlaycount( void );
#endif /* _MUSICMGR_H_ */
