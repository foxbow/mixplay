/**
 * interface between titles and the database
 */

#include "database.h"
#include "utils.h"
#include "mpgutils.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * turn a database entry into a mixplay structure
 */
static int db2entry( struct dbentry_t *dbentry, struct entry_t *entry ) {
	memset( entry, 0, DBESIZE );
	strcpy( entry->path, dbentry->path );
	strcpy( entry->artist, dbentry->artist );
	strcpy( entry->title, dbentry->title );
	strcpy( entry->album, dbentry->album );
	strcpy( entry->genre, dbentry->genre );
	snprintf( entry->display, MAXPATHLEN, "%s - %s", entry->artist, entry->title );
	entry->played=dbentry->played;
	return 0;
}

/**
 * pack a mixplay entry into a database structure
 */
static int entry2db( struct entry_t *entry, struct dbentry_t *dbentry ) {
	memset( dbentry, 0, DBESIZE );
	strcpy( dbentry->path, entry->path );
	strcpy( dbentry->artist, entry->artist );
	strcpy( dbentry->title, entry->title );
	strcpy( dbentry->album, entry->album );
	strcpy( dbentry->genre, entry->genre );
	dbentry->played=entry->played;
	return 0;
}

/**
 * opens the database file and handles errors
 */
int dbOpen( const char *path ){
	int db=-1;
	db = open( path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR );
	if( -1 == db ) {
		fail( errno, "Could not open database %s", path );
	}
	if( getVerbosity() > 1 ) printf("Opened database %s\n", path);
	return db;
}

/**
 * adds/overwrites a title to/in the database
 */
int dbPutTitle( int db, struct entry_t *title ){
	struct dbentry_t dbentry;

	if( 0 == db ){
		fail( F_FAIL, "%s - Database not open", __func__ );
	}

	if( 0 == title->key ) {
		lseek( db, 0, SEEK_END );
	}
	else {
		if( -1 == lseek( db, DBESIZE*(title->key-1), SEEK_SET ) ) {
			fail( errno, "Could not skip to title %s", title->path );
		}
	}
	entry2db( title, &dbentry );
	if( write( db, &dbentry, DBESIZE ) != DBESIZE ) {
		fail( errno, "Could not write entry %s!", title->path );
	}

	return 0;
}

/**
 * takes a database entry and adds it to a mixplay entry list
 * if there is no list, a new one will be created
 */
static struct entry_t *addDBTitle( struct dbentry_t dbentry, struct entry_t *root, unsigned long index ) {
	struct entry_t *entry;
	entry=malloc( sizeof( struct entry_t ) );
	if( NULL == entry ) {
		fail( errno, " %s - Could not create new entry", __func__ );
	}

	db2entry( &dbentry, entry );
	entry->key=index;

	if( NULL == root ) {
		root=entry;
		root->prev=root;
		root->next=root;
	}
	else {
		entry->next=root->next;
		entry->prev=root;
		root->next->prev=entry;
		root->next=entry;
	}

	return entry;
}

/**
 * move the current database file into a backup
 * tries to delete the backup first
 */
void dbBackup( const char *dbname ) {
	char backupname[MAXPATHLEN]="";

	strncpy( backupname, dbname, MAXPATHLEN );
	strncat( backupname, ".bak", MAXPATHLEN );
	if( rename(dbname, backupname) ) {
		fail( errno, "Could not rename %s", dbname );
	}
}

/**
 * gets all titles from the database and returns them as a mixplay entry list
 */
struct entry_t *dbGetMusic( const char *dbname ) {
	struct dbentry_t dbentry;
	unsigned long index = 1; // index 0 is reserved for titles not in the db!
	struct entry_t *dbroot=NULL;
	int db;
	db=dbOpen( dbname );

	while( read( db, &dbentry, DBESIZE ) == DBESIZE ) {
		dbroot = addDBTitle( dbentry, dbroot, index );
		index++;
	}

	dbClose( db );
	if( getVerbosity() ) printf("Loaded %li titles from the database\n", index-1 );

	return (dbroot?dbroot->next:NULL);
}

/**
 * checks for removed entries in the database
 * i.e. titles that are in the database but no longer on the medium
 */
int dbCheckExist( const char *dbname ) {
	struct entry_t *root;
	struct entry_t *runner;
	int num=0;

	root=dbGetMusic( dbname );

	do {
		activity( "Cleaning" );
		if( !mp3Exists(runner) ) {
			if(root == runner) root=runner->prev;
			runner=removeTitle( runner );
			num++;
		}
		else {
			runner=runner->next;
		}
	} while( ( runner != NULL ) && ( root != runner ) );

	if( num > 0 ) {
		dbDump( dbname, root );
		printf("Removed %i titles\n", num );
	}
	else {
		printf("No titles to remove\n" );
	}
	wipeTitles( root );

	return num;
}

/**
 * adds new titles to the database
 * before adding, the playcount is minimized, so that the new titles will mingle with
 * the least played titles.
 */
int dbAddTitles( const char *dbname, char *basedir ) {
	struct entry_t *fsroot;
	struct entry_t *dbroot;
	struct entry_t *dbrunner;
	unsigned long low;
	int num=0;
	int db=0;

	dbroot=dbGetMusic( dbname );

	db=dbOpen( dbname );
	if( NULL != dbroot ) {
		low=getLowestPlaycount( dbroot );
		if( 0 != low ) {
			do {
				activity("Smoothe playcount");
				if( ( dbrunner->flags & MP_FAV ) && ( dbrunner->played >= 2*low ) ) {
					dbrunner->played=dbrunner->played-(2*low);
				}
				else {
					dbrunner->played=dbrunner->played-low;
				}
				dbPutTitle( db, dbrunner );
				dbrunner=dbrunner->next;
			} while( dbrunner != dbroot );
		}
	}

	// scan directory
	fsroot=recurse(basedir, NULL, basedir);
	fsroot=fsroot->next;

	while( NULL != fsroot ) {
		activity("Adding");
		dbrunner = findTitle( dbroot, fsroot->path );
		if( NULL == dbrunner ) {
			fillTagInfo( basedir, dbrunner );
			dbPutTitle(db,fsroot);
			num++;
		}
		fsroot=removeTitle( fsroot );
	}

	printf("Added %i titles to %s\n", num, dbname );
	dbClose( db );
	return num;
}

void dbDump( const char *dbname, struct entry_t *root ) {
	int db;
	struct entry_t *runner=root;
	if( NULL == root ) {
		fail( F_FAIL, "Not dumping an empty database!");
	}
	dbBackup( dbname );
	db=dbOpen( dbname );
	do {
		dbPutTitle( db, runner );
		runner=runner->next;
	} while( runner != root );
	dbClose( db );
}

/**
 * closes the database file
 */
void dbClose( int db ) {
	close(db);
	return;
}
