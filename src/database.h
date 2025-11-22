/*
 * dbutils.h
 *
 *  Created on: 23.06.2016
 *	  Author: bweber
 */

#ifndef DATABASE_H_
#define DATABASE_H_
#include "musicmgr.h"
#include <assert.h>

typedef struct {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	char genre[NAMELEN];		/* Album info (from mp3) */
	uint32_t playcount;			/* play counter */
	uint32_t skipcount;			/* skip counter */
} dbentry_t;

#define DBESIZE sizeof(dbentry_t)
#define ESIZE sizeof(mptitle_t)

mptitle_t *dbGetMusic(void);
int32_t dbCheckExist(void);
int32_t dbAddTitles(char *basedir);
void dbWrite(int32_t);
int32_t dbNameCheck(void);
mptitle_t *getTitleByIndex(uint32_t index);
mptitle_t *getTitleForRange(const mpcmd_t range, const char *name);
void dbMarkDirty(void);
int32_t mp3Exists(const mptitle_t * title);
void dbAddPath(mptitle_t * title);

#endif /* DATABASE_H_ */
