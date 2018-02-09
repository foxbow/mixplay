#ifndef MUSICMGR_H_
#define MUSICMGR_H_

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

#define SL_UNSET   0
#define SL_TITLE   1
#define SL_ALBUM   2
#define SL_ARTIST  4
#define SL_GENRE   8
#define SL_PATH   16
#define SL_DISPLAY 32

typedef struct mptitle_t mptitle;
struct mptitle_t {
    char path[MAXPATHLEN];		/* path on the filesystem to the file */
    char artist[NAMELEN];		/* Artist info */
    char title[NAMELEN];		/* Title info (from mp3) */
    char album[NAMELEN];		/* Album info (from mp3) */
    unsigned int playcount;		/* play counter */
    unsigned int skipcount;		/* skip counter */
    char genre[NAMELEN];
    mptitle *plprev;		/* playlist pointer */
    unsigned int key;			/* DB key/index  - internal */
    char display[MAXPATHLEN];	/* Title display - internal */
    unsigned int flags;			/* 1=favourite   - internal */
    mptitle *plnext;
    mptitle *dbprev;		/* database pointer */
    mptitle *dbnext;
};

struct marklist_t {
    char dir[MAXPATHLEN+2];
    struct marklist_t *next;
};

/**
 * Music helper functions
 */
void addToFile( const char *path, const char *line, const char* prefix );

mptitle *addToPL( mptitle *title, mptitle *target );
mptitle *recurse( char *curdir, mptitle *files );
mptitle *shuffleTitles( mptitle *base );
mptitle *rewindTitles( mptitle *base );
mptitle *removeByPattern( mptitle *base, const char *pat );
mptitle *loadPlaylist( const char *path );
mptitle *insertTitle( mptitle *base, const char *path );
mptitle *skipTitles( mptitle *current, int num, const int global );
mptitle *searchList( mptitle *base, struct marklist_t *term );
int DNPSkip( mptitle *base, const unsigned int level );
int applyDNPlist( mptitle *base, struct marklist_t *list );
int applyFavourites( mptitle *root, struct marklist_t *list );
int markFavourite( mptitle *title, int range );
int searchPlay( mptitle *root, const char *pat, unsigned num );

mptitle *cleanTitles( mptitle *root );
struct marklist_t *cleanList( struct marklist_t *root );

void moveEntry( mptitle *entry, mptitle *pos );
void newCount( mptitle * root );
unsigned int getLowestPlaycount( mptitle *base, const int global );
struct marklist_t *loadList( const char *path );
int isMusic( const char *name );
struct marklist_t *addToList( const char *line, struct marklist_t **list );
void dumpTitles( mptitle *root, const int pl );
void dumpInfo( mptitle *root, int db, int skip );
int fillstick( mptitle *root, const char *target, int fav );
int writePlaylist( mptitle *root, const char *path );

#endif /* MUSICMGR_H_ */
