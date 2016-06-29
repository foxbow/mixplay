/*
 * dbutils.h
 *
 *  Created on: 23.06.2016
 *      Author: bweber
 */

#ifndef DBUTILS_H_
#define DBUTILS_H_
#include "musicmgr.h"
#include <stdio.h>
#include <fcntl.h>

struct dbentry_t {
	char path[MAXPATHLEN];		// path on the filesystem to the file
	unsigned long size;			// size in kb
	char artist[NAMELEN];		// Artist info
	char title[NAMELEN];		// Title info (from mp3)
	char album[NAMELEN];		// Album info (from mp3)
//	int  length;				// length in seconds (from mp3)
	unsigned long played;		// play counter
};

#define DBESIZE sizeof(struct dbentry_t)

int dbOpen( int *db, const char *path );
int dbAddTitle( int db, struct entry_t *title );
int dbSetTitle( int db, struct entry_t *title );
struct entry_t *dbGetMusic( int db );
int dbRemTitle( int db, struct entry_t *title );
void dbClose( int *db );

#endif /* DBUTILS_H_ */
