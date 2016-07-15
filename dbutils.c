#include "dbutils.h"

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
	entry->size=dbentry->size;
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
	dbentry->size=entry->size;
	return 0;
}

/**
 * opens the database fiel and handles errors
 */
int dbOpen( int *db, const char *path ){
	if( 0 != db[0] ) {
		fail( "Database already open!", path, F_FAIL );
	}
	db[0] = open( path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR );
	if( -1 == db[0] ) {
		fail( "Could not open database!", path, errno );
	}
	if( getVerbosity() > 1 ) printf("Opened database %s\n", path);
	return db[0];
}

/**
 * adds a title to the database
 */
int dbAddTitle( int db, struct entry_t *title ){
	struct dbentry_t dbentry;

	if( 0 == db ){
		fail("Database not open!", __func__, F_FAIL );
	}

	lseek( db, 0, SEEK_END );
	entry2db( title, &dbentry );
	if( write( db, &dbentry, DBESIZE ) != DBESIZE ) {
		fail( "Could not add entry!", title->path, errno );
	}

	return 0;
}

int dbSetTitle( int db, struct entry_t *title ) {
	struct dbentry_t dbentry;
	if( -1 == lseek( db, DBESIZE*title->key, SEEK_SET ) ) {
		fail( "Could not skip to title!", title->path, errno );
	}
	entry2db( title, &dbentry );
	if( write( db, &dbentry, DBESIZE ) != DBESIZE ) {
		fail( "Could not overwrite entry!", title->path, errno );
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
		fail("Could not create new entry", __func__, errno );
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
 * gets all titles from the database and returns them as a mixplay entry list
 */
struct entry_t *dbGetMusic( int db ) {
	struct dbentry_t dbentry;
	unsigned long index = 0;
	struct entry_t *dbroot=NULL;

	if( 0 == db ){
		fail("Database not open!", __func__, F_FAIL );
	}

	while( read( db, &dbentry, DBESIZE ) == DBESIZE ) {
		dbroot = addDBTitle( dbentry, dbroot, index );
		index++;
	}

	if( getVerbosity() ) printf("Loaded %li titles from the database\n", index );

	return (dbroot?dbroot->next:NULL);
}

int dbCheckExist( char *dbname ) {
	struct entry_t *root;
	struct entry_t *runner;
	int db=0;
	int num=0;

	dbOpen( &db, dbname );
	root=dbGetMusic( db );

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
	} while( root != runner );

	if( num > 0 ) {
		dbClose(&db);
		if( 0 != remove(dbname) ) {
			fail("Cannot delete", dbname, errno );
		}
		dbOpen(&db,dbname);
		while( NULL != runner ) {
			dbAddTitle( db, runner );
			runner=removeTitle( runner );
		}
		printf("Removed %i titles\n", num );
	}
	else {
		wipeTitles( root );
		printf("No titles to remove\n" );
	}
	dbClose( &db );

	return num;
}

int dbAddTitles( const char *dbname, char *basedir ) {
	struct entry_t *fsroot;
	struct entry_t *dbroot;
	struct entry_t *dbrunner;
	int num=0;
	int db=0;

	dbOpen( &db, dbname );
	dbroot=dbGetMusic(db);

	// scan directory
	fsroot=recurse(basedir, NULL, basedir);
	fsroot=fsroot->next;

	while( NULL != fsroot ) {
		activity("Adding");
		dbrunner = findTitle( dbroot, fsroot->path );
		if( NULL == dbrunner ) {
			dbAddTitle(db,fsroot);
			num++;
		}
		fsroot=removeTitle( fsroot );
	}

	printf("Added %i titles to %s\n", num, dbname );
	dbClose( &db );
	return num;
}

/**
 * closes the database file
 */
void dbClose( int *db ) {
	if( 0 == db ){
		fail("Database not open!", __func__, F_FAIL );
	}
	close(db[0]);
	db[0]=0;

	return;
}
