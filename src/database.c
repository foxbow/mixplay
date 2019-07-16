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

void dbMarkDirty( void ) {
	if( getConfig()->dbDirty++ > 25 ) {
		dbWrite( );
	}
}

mptitle *getTitleByIndex( unsigned int index ) {
	mptitle *root=getConfig()->root;
	mptitle *run=root;
	if( root == NULL ) {
		return NULL;
	}

	do {
		if( run->key == index ) {
			return run;
		}
		run=run->next;
	} while ( root != run );

	return NULL;
}

/**
 * searches for a title that fits the name in the range.
 * This is kind of a hack to turn an artist name or an album name into
 * a title that can be used for FAV/DNP marking.
 */
mptitle *getTitleForRange( const mpcmd range, const char *name ) {
	mptitle *root=getConfig()->root;
	mptitle *run=root;

	if( root == NULL ) {
		return NULL;
	}

	if( !MPC_EQALBUM(range) && !MPC_EQARTIST(range) ){
		addMessage(0, "Can only search represantatives for Artists and Albums!");
		return NULL;
	}

	do {
		if( MPC_EQALBUM( range ) && !strcasecmp( run->album, name ) ) {
			return run;
		}
		if( MPC_EQARTIST( range ) && !strcasecmp( run->artist, name ) ) {
			return run;
		}
		run=run->next;
	} while ( root != run );

	return NULL;
}

/**
 * 	searches for a given path in the mixplay entry list
 */
static mptitle *findTitle( mptitle *base, const char *path ) {
	mptitle *runner;

	if( NULL == base ) {
		return NULL;
	}

	runner=base;

	do {
		if( strstr( runner->path, path ) ) {
			return runner;
		}

		runner=runner->next;
	}
	while ( runner != base );

	return NULL;
}

/**
 * checks if a given title still exists on the filesystem
 */
int mp3Exists( const mptitle *title ) {
	return( access( fullpath(title->path), F_OK ) == 0 );
}

/**
 * deletes an entry from the database list
 * This should only be used on a database cleanup!
 */
static mptitle *removeTitle( mptitle *entry ) {
	mptitle *next=NULL;

	if( entry->next != entry ) {
		next=entry->next;
		entry->next->prev=entry->prev;
		entry->prev->next=entry->next;
	}

	free( entry );
	return next;
}


/**
 * turn a database entry into a mixplay structure
 */
static void db2entry( struct dbentry_t *dbentry, mptitle *entry ) {
	memset( entry, 0, ESIZE );
	strcpy( entry->path, dbentry->path );
	strcpy( entry->artist, dbentry->artist );
	strcpy( entry->title, dbentry->title );
	strcpy( entry->album, dbentry->album );
	strcpy( entry->genre, dbentry->genre );
	snprintf( entry->display, MAXPATHLEN, "%s - %s", entry->artist, entry->title );
	entry->playcount=dbentry->playcount;
	entry->skipcount=dbentry->skipcount;
}

/**
 * pack a mixplay entry into a database structure
 */
static void entry2db( mptitle *entry, struct dbentry_t *dbentry ) {
	memset( dbentry, 0, DBESIZE );
	strcpy( dbentry->path, entry->path );
	strcpy( dbentry->artist, entry->artist );
	strcpy( dbentry->title, entry->title );
	strcpy( dbentry->album, entry->album );
	strcpy( dbentry->genre, entry->genre );
	dbentry->playcount=entry->playcount;
	dbentry->skipcount=entry->skipcount;
}

/**
 * opens the database file and handles errors
 */
int dbOpen( const char *path ) {
	int db=-1;
	db = open( path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR );

	if( -1 == db ) {
		fail( errno, "Could not open database %s", path );
	}

	addMessage( 2, "Opened database %s", path );
	return db;
}

/**
 * adds/overwrites a title to/in the database
 */
static int dbPutTitle( int db, mptitle *title ) {
	struct dbentry_t dbentry;

	if( 0 == db ) {
		fail( F_FAIL, "%s - Database not open", __func__ );
	}

	if( 0 == title->key ) {
		lseek( db, 0, SEEK_END );
	}
	else {
		if( -1 == lseek( db, DBESIZE*( title->key-1 ), SEEK_SET ) ) {
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
static mptitle *addDBTitle( struct dbentry_t dbentry, mptitle *root, unsigned int index ) {
	mptitle *entry;
	entry=(mptitle*)malloc( sizeof( mptitle ) );

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
 * gets all titles from the database and returns them as a mixplay entry list
 */
mptitle *dbGetMusic( const char *dbname ) {
	struct dbentry_t dbentry;
	unsigned int index = 1; /* index 0 is reserved for titles not in the db! */
	mptitle *dbroot=NULL;
	int db;
	size_t len;
	db=dbOpen( dbname );

	while( ( len = read( db, &dbentry, DBESIZE ) ) == DBESIZE ) {
		/* support old database title path format */
		if( ( dbentry.path[0] == '/' ) &&
		    ( strstr( dbentry.path, getConfig()->musicdir ) != dbentry.path ) ) {
			strcpy( dbentry.path, fullpath(&(dbentry.path[1])));
			getConfig()->dbDirty=1;
		}
		dbroot = addDBTitle( dbentry, dbroot, index );
		index++;
	}

	dbClose( db );

	if( 0 != len ) {
		fail( F_FAIL, "Database %s is corrupt!\nRun 'mixplay -C' to rescan", dbname );
	}

	addMessage( 1, "Loaded %i titles from the database", index-1 );

	return ( dbroot?dbroot->next:NULL );
}

/**
 * checks for removed entries in the database
 * i.e. titles that are in the database but no longer on the medium
 */
int dbCheckExist( void ) {
	mptitle *root;
	mptitle *runner;
	int num=0;

	root=getConfig()->root;
	if( root == NULL ) {
		addMessage( -1, "No music in database!" );
		return -1;
	}
	runner=root;
	addMessage( 1, "Cleaning database..." );

	do {
		activity( "Cleaning" );

		if( !mp3Exists( runner ) ) {
			if( root == runner ) {
				root=runner->prev;
			}

			runner=removeTitle( runner );
			num++;
		}
		else {
			runner=runner->next;
		}
	}
	while( ( runner != NULL ) && ( root != runner ) );

	if( num > 0 ) {
		dbMarkDirty();
	}

	return num;
}

static int dbAddTitle( int db, mptitle *title ) {
    struct dbentry_t dbentry;

    if( 0 == db ) {
        fail( F_FAIL, "%s - Database not open", __func__ );
    }

    lseek( db, 0, SEEK_END );

    entry2db( title, &dbentry );

    if( write( db, &dbentry, DBESIZE ) != DBESIZE ) {
        fail( errno, "Could not write entry %s!", title->path );
    }

    return 0;
}


/**
 * adds new titles to the database
 * the new titles will have a playcount set to blend into the mix
 */
int dbAddTitles( const char *dbname, char *basedir ) {
	mptitle *fsroot;
	mptitle *fsnext;
	mptitle *dbroot;
	mptitle *dbrunner;
	unsigned count=0, mean=0;
	unsigned index=0;
	int num=0, db=0;

	dbroot=getConfig()->root;
	if( dbroot == NULL ) {
		addMessage(0, "No database loaded!" );
	}
	else {
		dbrunner=dbroot;

		do {
			/* find highest key */
			if( index < dbrunner->key ) {
				index=dbrunner->key;
			}
			/* find mean playcount */
			if( !( dbrunner->flags & MP_DNP ) ) {
				count++;
				mean+=dbrunner->playcount;
			}
			dbrunner=dbrunner->next;
		}
		while( dbrunner != dbroot );

		/* round down so new titles have a slightly better chance to be played
		   and to equalize favourites */
		mean=(mean/count);

	}

	addMessage( 0, "Using mean playcount %d", mean );
	addMessage( 0, "%d titles in current database", index );
	index++;

	/* scan directory */
	addMessage( 0, "Scanning..." );
	fsroot=recurse( basedir, NULL );
	if( fsroot == NULL ) {
		addMessage( -1, "No music found in %s!", basedir );
		return 0;
	}

	fsroot=fsroot->next;

	addMessage( 0, "Adding titles..." );

	db=dbOpen( dbname );
	while( NULL != fsroot ) {
		activity( "Adding" );
		dbrunner = findTitle( dbroot, fsroot->path );

		if( NULL == dbrunner ) {
			fsnext=fsroot->next;
			/* This was the last title in fsroot */
			if( fsnext == fsroot ) {
				fsnext=NULL;
			}

			fsroot->playcount=mean;
			fsroot->key=index++;
			addMessage(1, "Adding %s", fsroot->display );
			dbAddTitle( db, fsroot );

			/* unlink title from fsroot */
			fsroot->prev->next=fsroot->next;
			fsroot->next->prev=fsroot->prev;

			/* add title to dbroot */
			if( getProfile()->favplay ) {
				fsroot->flags=MP_DNP;
			}
			if( dbroot == NULL ) {
				dbroot=fsroot;
				dbroot->prev=dbroot;
				dbroot->next=dbroot;
			}
			else {
				dbroot->prev->next=fsroot;
				fsroot->prev=dbroot->prev;
				fsroot->next=dbroot;
				dbroot->prev=fsroot;
			}
			num++;

			fsroot=fsnext;
		}
		else {
			fsroot=removeTitle( fsroot );
		}
	}
	dbClose( db );

	if( getConfig()->root == NULL ) {
		addMessage(0, "Setting new active database" );
		getConfig()->root=dbroot;
	}

	return num;
}

static int checkPath( mptitle *entry, int range ) {
	char	path[MAXPATHLEN];
	char	check[NAMELEN]="";
	char *pos;

	strltcpy( path, entry->path, MAXPATHLEN );
	pos=strrchr( path, '/' );
	if( NULL != pos ) {
		*pos=0;
	}

	switch( range ) {
	case mpc_artist:
		strltcpy( check,  entry->artist, NAMELEN );
		break;
	case mpc_album:
		strltcpy( check,  entry->artist, NAMELEN );
		break;
	default:
		fail( F_FAIL, "checkPath() range %i not implemented!\n", range );
		break;
	}
	return( strstr( path, check ) != NULL );
}


int dbNameCheck( const char *dbname ) {
	mptitle	*root;
	mptitle	*currentEntry;
	mptitle	*runner;
	int				count=0;
	FILE 			*fp;
	int				match;

	root=dbGetMusic( dbname );
	if( root == NULL ) {
		addMessage( -1, "No music in database!");
		return -1;
	}

	fp=fopen( "rmlist.sh", "w" );
	if( NULL == fp ) {
		fail( errno, "Could not open rmlist.sh for writing " );
	}

	fprintf( fp, "#!/bin/bash\n" );

	currentEntry=root;
	while( currentEntry->next != root ) {
		activity("Namechecking - %i", count );
		if( !(currentEntry->flags & MP_MARK) ) {
			runner=currentEntry->next;
			do {
				if( !(runner->flags & MP_MARK ) ) {
					if( strcmp( runner->display, currentEntry->display ) == 0 ) {
						match=0;
						if( checkPath( runner, mpc_artist ) ) {
							match|=1;
						}
						if ( checkPath( runner, mpc_album ) ){
							match|=2;
						}
						if( checkPath( currentEntry, mpc_artist ) ){
							match|=4;
						}
						if( checkPath( currentEntry, mpc_album ) ){
							match|=8;
						}

						switch( match ) {
						case  1: /* 0001 */
						case  2: /* 0010 */
						case  3: /* 0011 */
						case  7: /* 0111 */
						case 11: /* 1011 */
							unlink( currentEntry->path );
							addMessage( 1, "removed %s", currentEntry->path );
							runner->flags |= MP_MARK;
							count++;
							break;
						case  4: /* 0100 */
						case  8: /* 1000 */
						case 12: /* 1100 */
						case 13: /* 1101 */
						case 14: /* 1110 */
							unlink( runner->path );
							addMessage( 1, "removed %s", runner->path );
							currentEntry->flags |= MP_MARK;
							count++;
							break;
						case  0: /* 0000 */
						case  5: /* 0101 */
						case  6: /* 0110 */
						case  9: /* 1001 */
						case 10: /* 1010 */
							fprintf( fp, "## %i\n", match );
							fprintf( fp, "#rm \"%s\"\n", currentEntry->path );
							fprintf( fp, "#rm \"%s\"\n\n", runner->path );
							runner->flags |= MP_MARK; /* make sure only one of the doublets is used for future checkings */
							break;
						case 15: /* 1111 */
							/* both titles are fine! */
							break;
						default:
							fail( F_FAIL, "Incorrect match: %i\n", match );
							break;
						}
					}
				}
				if( currentEntry->flags & MP_MARK ) {
					runner=root;
				}
				else {
					runner=runner->next;
				}
			} while( runner != root );
		}
		currentEntry=currentEntry->next;
	}
	fprintf( fp, "\necho Remember to clean the database!\n\n" );

	fclose( fp );

	return count;
}

/**
 * closes the database file
 */
void dbClose( int db ) {
	close( db );
	return;
}

/**
 * Creates a backup of the current database file and dumps the
 * current reindexed database in a new file
 */
void dbWrite( void ) {
	int db;
	unsigned int index=1;
	const char *dbname=getConfig()->dbname;
	mptitle *root=getConfig()->root;
	mptitle *runner=root;

	if( NULL == root ) {
		fail( F_FAIL, "Not dumping an empty database!" );
	}

	fileBackup( dbname );
	db=dbOpen( dbname );

	do {
		runner->key=index;
		dbPutTitle( db, runner );
		index++;
		runner=runner->next;
	}
	while( runner != root );

	dbClose( db );
	getConfig()->dbDirty=0;
	/* start from the beginning so we catch remainders */
	getConfig()->playcount=0;
}
