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
 * reates a backup of the current database file and dumps the
 * current database in a new file
 */
static void dbDump( const char *dbname, struct entry_t *root ) {
	int db;
	struct entry_t *runner=root;
	if( NULL == root ) {
		fail( F_FAIL, "Not dumping an empty database!");
	}
	dbBackup( dbname );
	db=dbOpen( dbname );
	do {
		dbPutTitle( db, runner );
		runner=runner->dbnext;
	} while( runner != root );
	dbClose( db );
}

/**
 * checks if a given title still exists on the filesystem
 */
static int mp3Exists( const struct entry_t *title ) {
	return( access( title->path, F_OK ) );
}

/**
 * clean up a list of entries
 */
static void wipeTitles( struct entry_t *files ){
	struct entry_t *buff=files;
	if( NULL == files ) return;

	files->dbprev->dbnext=NULL;

	while( buff != NULL ){
		files=buff;
		buff=buff->dbnext;
		free(files);
	}
}

/**
 * deletes an entry from the database list
 * This should only be used on a database cleanup!
 */
static struct entry_t *removeTitle( struct entry_t *entry ) {
	struct entry_t *next=NULL;
	if( entry->plnext != entry ) {
		entry->plnext->plprev=entry->plprev;
		entry->plprev->plnext=entry->plnext;
	}

	if( entry->dbnext != entry ) {
		next=entry->dbnext;
		entry->dbnext->dbprev=entry->dbprev;
		entry->dbprev->dbnext=entry->dbnext;
	}
	free(entry);
	return next;
}


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
	entry->skipped=dbentry->skipped;
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
	dbentry->skipped=entry->skipped;
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
static struct entry_t *addDBTitle( struct dbentry_t dbentry, struct entry_t *root, unsigned int index ) {
	struct entry_t *entry;
	entry=malloc( sizeof( struct entry_t ) );
	if( NULL == entry ) {
		fail( errno, " %s - Could not create new entry", __func__ );
	}

	db2entry( &dbentry, entry );
	entry->key=index;

	if( NULL == root ) {
		root=entry;
		root->dbprev=root;
		root->dbnext=root;
	}
	else {
		entry->dbnext=root->dbnext;
		entry->dbprev=root;
		root->dbnext->dbprev=entry;
		root->dbnext=entry;
		entry->plnext=entry;
		entry->plprev=entry;
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
	unsigned int index = 1; // index 0 is reserved for titles not in the db!
	struct entry_t *dbroot=NULL;
	int db;
	size_t len;
	db=dbOpen( dbname );

	while( (len = read( db, &dbentry, DBESIZE )) == DBESIZE ) {
		dbroot = addDBTitle( dbentry, dbroot, index );
		index++;
	}
	dbClose( db );

	if( 0 != len ) {
		fail( F_FAIL, "Database %s is corrupt!\nRun 'mixplay -C' to rescan", dbname );
	}

	if( getVerbosity() ) printf("Loaded %i titles from the database\n", index-1 );

	return (dbroot?dbroot->dbnext:NULL);
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
			if(root == runner) root=runner->dbprev;
			runner=removeTitle( runner );
			num++;
		}
		else {
			runner=runner->dbnext;
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
	unsigned int low;
	int num=0;
	int db=0;

	dbroot=dbGetMusic( dbname );

	db=dbOpen( dbname );
	if( NULL != dbroot ) {
		low=getLowestPlaycount( dbroot, -1 );
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
				dbrunner=dbrunner->dbnext;
			} while( dbrunner != dbroot );
		}
	}

	// scan directory
	fsroot=recurse(basedir, NULL, basedir);
	fsroot=fsroot->dbnext;

	while( NULL != fsroot ) {
		activity("Adding");
		dbrunner = findTitle( dbroot, fsroot->path );
		if( NULL == dbrunner ) {
			fillTagInfo( basedir, fsroot );
			dbPutTitle(db,fsroot);
			num++;
		}
		fsroot=removeTitle( fsroot );
	}

	printf("Added %i titles to %s\n", num, dbname );
	dbClose( db );
	return num;
}

/**
 * closes the database file
 */
void dbClose( int db ) {
	close(db);
	return;
}
