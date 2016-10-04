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
	char artist[NAMELEN];		// Artist info
	char title[NAMELEN];		// Title info (from mp3)
	char album[NAMELEN];		// Album info (from mp3)
	char genre[NAMELEN];		// Album info (from mp3)
	unsigned long played;		// play counter
};

#define DBESIZE sizeof(struct dbentry_t)

int dbOpen( int *db, const char *path );
int dbPutTitle( int db, struct entry_t *title );
struct entry_t *dbGetMusic( int db );
int dbRemTitle( int db, struct entry_t *title );
int dbCheckExist( char *dbname );
int dbAddTitles( const char *dbname, char *basedir );
void dbClose( int *db );
void dbBackup( char *dbname );
void dbDump( char *dbname, struct entry_t *root );

#endif /* DBUTILS_H_ */
