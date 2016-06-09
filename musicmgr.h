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

#define RANDOM(x) (rand()%x)
#ifndef MAXPATHLEN
  #define MAXPATHLEN 256
#endif

struct entry_t {
	struct entry_t *prev;
	unsigned long key;			// DB key/index
	char path[MAXPATHLEN];		// path on the filesystem to the file
	char name[MAXPATHLEN];		// filename
	char display[MAXPATHLEN];	// displayname (artist - title)
	unsigned long size;			// size in kb
	char artist[MAXPATHLEN];	// Artist info
	int  rating;				// 1=favourite
	char title[MAXPATHLEN];		// Title info (from mp3)
	char album[MAXPATHLEN];		// Album info (from mp3)
	int  length;				// length in seconds (from mp3)
	unsigned long played;		// play counter
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
struct entry_t *recurse( char *curdir, struct entry_t *files );
struct entry_t *shuffleTitles( struct entry_t *base );
struct entry_t *rewindTitles( struct entry_t *base );
struct entry_t *removeTitle( struct entry_t *entry );
struct entry_t *loadPlaylist( const char *path );
struct entry_t *insertTitle( struct entry_t *base, const char *path );
struct entry_t *skipTitles( struct entry_t *current, int num, int repeat, int mix );
struct entry_t *useBlacklist( struct entry_t *base );
int countTitles( struct entry_t *base );
int loadBlacklist( const char *path );
int loadWhitelist( const char *path );
int genPathName( char *name, const char *cd, const size_t len );
int isMusic( const char *name );
int addToWhitelist( const char *line );
int checkWhitelist( struct entry_t *root );
int mp3Exists( const struct entry_t *title );
int getFiles( const char *cd, struct dirent ***filelist );
int getDirs( const char *cd, struct dirent ***dirlist );

// DEBUG only!
// void dumpTitles( struct entry_t *root, char *msg );
#endif /* MUSICMGR_H_ */
