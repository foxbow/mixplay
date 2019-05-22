/*
 * dbutils.h
 *
 *  Created on: 23.06.2016
 *	  Author: bweber
 */

#ifndef DATABASE_H_
#define DATABASE_H_
#include "musicmgr.h"

struct dbentry_t {
	char path[MAXPATHLEN];		/* path on the filesystem to the file */
	char artist[NAMELEN];		/* Artist info */
	char title[NAMELEN];		/* Title info (from mp3) */
	char album[NAMELEN];		/* Album info (from mp3) */
	char genre[NAMELEN];		/* Album info (from mp3) */
	unsigned int playcount;		/* play counter */
	unsigned int skipcount;		/* skip counter */
};

#define DBESIZE sizeof(struct dbentry_t)
#define ESIZE sizeof(mptitle)

int dbOpen( const char *path );
mptitle *dbGetMusic( const char *dbname );
int dbRemTitle( int db, mptitle *title );
int dbCheckExist( const char *dbname );
int dbAddTitles( const char *dbname, char *basedir );
void dbClose( int db );
void dbWrite( void );
int dbNameCheck( const char *dbname );
mptitle *getTitleByIndex( unsigned int index );
mptitle *getTitleForRange( const mpcmd range, const char *name );
void dbMarkDirty( void );
int mp3Exists( const mptitle *title );

/* void dbDump( const char *dbname, mptitle *root ); */

#endif /* DATABASE_H_ */
