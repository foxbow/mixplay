#ifndef MUSICMGR_H_
#define MUSICMGR_H_

#include "utils.h"

/* Directory access */
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <sys/time.h>

#define NAMELEN 64

#define RANDOM(x) (rand()%x)
#ifndef MAXPATHLEN
  #define MAXPATHLEN 256
#endif

#define MP_FAV 1

struct entry_t {
	char path[MAXPATHLEN];		// path on the filesystem to the file
	unsigned long size;			// size in kb
	char artist[NAMELEN];	// Artist info
	char title[NAMELEN];		// Title info (from mp3)
	char album[NAMELEN];		// Album info (from mp3)
//	int  length;				// length in seconds (from mp3)
	unsigned long played;		// play counter
	struct entry_t *prev;		//
	unsigned long key;			// DB key/index  - internal
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
// unused?!
void wipeTitles( struct entry_t *files );
struct entry_t *recurse( char *curdir, struct entry_t *files, const char *basedir );
struct entry_t *shuffleTitles( struct entry_t *base );
struct entry_t *rewindTitles( struct entry_t *base );
struct entry_t *removeTitle( struct entry_t *entry );
struct entry_t *loadPlaylist( const char *path );
struct entry_t *insertTitle( struct entry_t *base, const char *path );
struct entry_t *skipTitles( struct entry_t *current, int num );
struct entry_t *useBlacklist( struct entry_t *base );
struct entry_t *useWhitelist( struct entry_t *base );
int countTitles( struct entry_t *base );
int loadBlacklist( const char *path );
int loadWhitelist( const char *path );
int genPathName( const char *basedir, struct entry_t *entry  );
int isMusic( const char *name );
int addToWhitelist( const char *line );
int checkWhitelist( struct entry_t *root );
int mp3Exists( const struct entry_t *title );
int getFiles( const char *cd, struct dirent ***filelist );
int getDirs( const char *cd, struct dirent ***dirlist );

#ifdef DEBUG
void dumpTitles( struct entry_t *root, char *msg );
#endif
#endif /* MUSICMGR_H_ */
