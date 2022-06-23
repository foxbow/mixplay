#ifndef _MUSICMGR_H_
#define _MUSICMGR_H_

#include <dirent.h>

/* for MAXPATHLEN */
#include "utils.h"
/* Directory access */

#define NAMELEN 64
/* do not return more than 100 titles */
#define MAXSEARCH 100

/* similarity index for checksim */
#define SIMGUARD 70

/* flags - keep clear of range bits! */
#define MP_NONE  0x0000
#define MP_FAV   0x0001			/* Favourite */
#define MP_DNP   0x0002			/* do not play */
#define MP_DBL   0x0004			/* doublet */
#define MP_INPL  0x0008			/* was added normally to the playlist */
#define MP_PDARK 0x0010			/* does not fit the playcount */
#define MP_TDARK 0x0020			/* does not fit the titles */
#define MP_ALL   0x003f
#define MP_MARK  0x0040
// mpc_genre     0x0100
// mpc_artist    0x0200
// mpc_album     0x0400
// mpc_title     0x0800
// mpc_display   0x1000
#define MP_HIDE  (MP_DNP|MP_DBL|MP_INPL|MP_TDARK)

typedef struct mptitle_s mptitle_t;
struct mptitle_s {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	uint32_t playcount;			/* play counter */
	uint32_t favpcount;			/* transient favplaycount */
	uint32_t skipcount;			/* skip counter */
	char genre[NAMELEN];
	uint32_t key;				/* DB key/index  - internal */
	char display[MAXPATHLEN];	/* Title display - internal */
	uint32_t flags;				/* FAV/DNP       - internal */
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
	uint32_t tnum;
	uint32_t anum;
	uint32_t lnum;
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
mpplaylist_t *addToPL(mptitle_t * title, mpplaylist_t * target,
					  const int32_t mark);
mpplaylist_t *remFromPLByKey(const uint32_t key);
mpplaylist_t *addPLDummy(mpplaylist_t * pl, const char *name);
void plCheck(int32_t del);
int32_t writePlaylist(mpplaylist_t * pl, const char *name);

mptitle_t *recurse(char *curdir, mptitle_t * files);
mptitle_t *rewindTitles(mptitle_t * base);
mptitle_t *loadPlaylist(const char *path);
mptitle_t *insertTitle(mptitle_t * base, const char *path);
int32_t search(const mpcmd_t range, const char *pat);

void moveTitleByIndex(uint32_t from, uint32_t before);

int32_t playCount(mptitle_t * title, int32_t skip);
void applyLists(int32_t clean);
int32_t searchPlay(const char *pat, uint32_t num, const int32_t global);
int32_t handleRangeCmd(mpcmd_t cmd, mptitle_t * title);
int32_t handleDBL(mptitle_t * title);
int32_t applyDNPlist(marklist_t * list, int32_t dbl);
int32_t getListPath(mpcmd_t cmd, char path[MAXPATHLEN]);

marklist_t *loadList(const mpcmd_t cmd);
int32_t delFromList(const mpcmd_t cmp, const char *line);
int32_t writeList(const mpcmd_t cmd);
int32_t delTitleFromOtherList(mpcmd_t cmd, const mptitle_t * title);

bool isMusic(const char *name);
void dumpTitles(mptitle_t * root, const int32_t pl);
void dumpInfo(int32_t smooth);
void setArtistSpread();
int32_t fillstick(mptitle_t * root, const char *target);
int32_t getPlaylists(const char *cd, struct dirent ***pllist);
uint64_t countTitles(const uint32_t inc, const uint32_t exc);

#define countflag(x) countTitles((x), MP_NONE)
uint32_t getPlaycount(int32_t high);

void dumpState(void);

/* exported for unittests */
int32_t checkSim(const char *text, const char *pat);
#endif /* _MUSICMGR_H_ */
