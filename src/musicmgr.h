#ifndef _MUSICMGR_H_
#define _MUSICMGR_H_

#include <dirent.h>

/* for MAXPATHLEN */
#include "utils.h"
/* Directory access */

#define NAMELEN 64
/* do not return more than 100 titles */
#define MAXSEARCH 100

/* flags */
#define MP_FAV   1				/* Favourite */
#define MP_DNP   2				/* do not play */
#define MP_DBL   4				/* doublet */
#define MP_MARK  8				/* was added normally to the playlist */
#define MP_ALL   31

typedef struct mptitle_s mptitle_t;
struct mptitle_s {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	unsigned playcount;			/* play counter */
	unsigned favpcount;			/* transient favplaycount */
	unsigned skipcount;			/* skip counter */
	char genre[NAMELEN];
	unsigned key;				/* DB key/index  - internal */
	char display[MAXPATHLEN];	/* Title display - internal */
	unsigned flags;				/* FAV/DNP       - internal */
	mptitle_t *prev;			/* database pointers */
	mptitle_t *next;
};

typedef struct mpplaylist_s mpplaylist_t;
struct mpplaylist_s {
	mptitle_t *title;
	mpplaylist_t *prev;
	mpplaylist_t *next;
};

typedef enum {
	mpsearch_idle,
	mpsearch_busy,
	mpsearch_done
} mpsearch_t;

typedef struct {
	unsigned tnum;
	unsigned anum;
	unsigned lnum;
	mpplaylist_t *titles;
	char **artists;
	char **albums;
	char **albart;
	mpsearch_t state;
} searchresults_t;

typedef struct marklist_s marklist_t;
struct marklist_s {
	char dir[MAXPATHLEN + 2];
	marklist_t *next;
};

#include "config.h"

/**
 * playlist functions
 */
mpplaylist_t *appendToPL(mptitle_t * title, mpplaylist_t * pl, const int mark);
mpplaylist_t *addToPL(mptitle_t * title, mpplaylist_t * target,
					  const int mark);
mpplaylist_t *remFromPLByKey(mpplaylist_t * root, const unsigned key);
mpplaylist_t *addPLDummy(mpplaylist_t * pl, const char *name);
void plCheck(int del);
int writePlaylist(mpplaylist_t * pl, const char *name);

mptitle_t *recurse(char *curdir, mptitle_t * files);
mptitle_t *rewindTitles(mptitle_t * base);
mptitle_t *loadPlaylist(const char *path);
mptitle_t *insertTitle(mptitle_t * base, const char *path);
int search(const char *pat, const mpcmd_t range);

void moveTitleByIndex(unsigned from, unsigned before);

int playCount(mptitle_t * title, int skip);
void applyLists(int clean);
int searchPlay(const char *pat, unsigned num, const int global);
int handleRangeCmd(mptitle_t * title, mpcmd_t cmd);
int handleDBL(mptitle_t * title);
int applyDNPlist(marklist_t * list, int dbl);
int addRangePrefix(char *line, mpcmd_t cmd);
int getListPath(char path[MAXPATHLEN], mpcmd_t cmd);

marklist_t *loadList(const mpcmd_t cmd);
int delFromList(const mpcmd_t cmp, const char *line);
int writeList(const mpcmd_t cmd);

int isMusic(const char *name);
void dumpTitles(mptitle_t * root, const int pl);
void dumpInfo(mptitle_t * root, int smooth);
int fillstick(mptitle_t * root, const char *target);
int getPlaylists(const char *cd, struct dirent ***pllist);
unsigned long countTitles(const unsigned int inc, const unsigned int exc);
unsigned getPlaycount(int high);
#endif /* _MUSICMGR_H_ */
