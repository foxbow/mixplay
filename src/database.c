/**
 * interface between titles and the database
 */
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "database.h"
#include "utils.h"
#include "mpgutils.h"

/**
 * closes the database file
 */
static void dbClose( int db ) {
	close( db );
	return;
}

void dbMarkDirty( void ) {
	/* is there a database in use at all? */
	if( !(getConfig()->mpmode & PM_DATABASE)) {
		return;
	}
	/* ignore changes to the database in favplay mode */
	if( getConfig()->dbDirty++ > 25 ) {
		dbWrite( );
	}
}

mptitle_t *getTitleByIndex( unsigned int index ) {
	mptitle_t *root=getConfig()->root;
	mptitle_t *run=root;
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
mptitle_t *getTitleForRange( const mpcmd_t range, const char *name ) {
	/* start with the current title so we can notice it disappear on DNP */
	mptitle_t *root=getConfig()->current->title;
	mptitle_t *run=root;

	if( root == NULL ) {
		return NULL;
	}

	if( !MPC_EQALBUM(range) && !MPC_EQARTIST(range) ){
		addMessage( 0, "Can only search represantatives for Artists and Albums!");
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
static mptitle_t *findTitle( mptitle_t *base, const char *path ) {
	mptitle_t *runner;

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
int mp3Exists( const mptitle_t *title ) {
	return( access( fullpath(title->path), F_OK ) == 0 );
}

/**
 * deletes an entry from the database list
 * This should only be used on a database cleanup!
 */
static mptitle_t *removeTitle( mptitle_t *entry ) {
	mptitle_t *next=NULL;

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
static void db2entry( dbentry_t *dbentry, mptitle_t *entry ) {
	memset( entry, 0, ESIZE );
	strcpy( entry->path, dbentry->path );
	strcpy( entry->artist, dbentry->artist );
	strcpy( entry->title, dbentry->title );
	strcpy( entry->album, dbentry->album );
	strcpy( entry->genre, dbentry->genre );
	strtcpy( entry->display, dbentry->artist, MAXPATHLEN-1 );
	strtcat( entry->display, " - ", MAXPATHLEN-1 );
	strtcat( entry->display, dbentry->title, MAXPATHLEN-1 );
	entry->playcount=dbentry->playcount;
	entry->skipcount=dbentry->skipcount;
	entry->favpcount=0;
}

/**
 * pack a mixplay entry into a database structure
 */
static void entry2db( mptitle_t *entry, dbentry_t *dbentry ) {
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
 * opens the database file
 */
static int dbOpen( ) {
	int db=-1;
	char *path = getConfig()->dbname;

	db = open( path, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR );

	if( -1 == db ) {
		addMessage( -1, "Could not open database %s", path );
	}
	else {
		addMessage( 2, "Opened database %s", path );
	}
	return db;
}

/**
 * adds/overwrites a title to/in the database
 */
static int dbPutTitle( int db, mptitle_t *title ) {
	dbentry_t dbentry;

	assert( 0 != db );

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
static mptitle_t *addDBTitle( dbentry_t *dbentry, mptitle_t *root, unsigned int index ) {
	mptitle_t *entry;
	entry=(mptitle_t*)falloc( 1, sizeof( mptitle_t ) );

	db2entry( dbentry, entry );
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
mptitle_t *dbGetMusic( ) {
	dbentry_t dbentry;
	unsigned int index = 1; /* index 0 is reserved for titles not in the db! */
	mptitle_t *dbroot=NULL;
	int db;
	size_t len;
	db=dbOpen( );
	if( db == -1 ) {
		return NULL;
	}

	activity(1, "Loading database");
	while( ( len = read( db, &dbentry, DBESIZE ) ) == DBESIZE ) {
		/* explicitlöy terminate path */
		dbentry.path[MAXPATHLEN-1]=0;
		/* support old database title path format */
		if( ( dbentry.path[0] == '/' ) &&
		    ( strstr( dbentry.path, getConfig()->musicdir ) != dbentry.path ) ) {
			strtcpy( dbentry.path, fullpath(&(dbentry.path[1])), MAXPATHLEN );
			getConfig()->dbDirty=1;
		}
		dbroot = addDBTitle( &dbentry, dbroot, index );
		index++;
	}

	dbClose( db );

	if( 0 != len ) {
		addMessage(0, "Database is corrupt, trying backup.");
		dbroot=wipeTitles(dbroot);
		if( !fileRevert( getConfig()->dbname ) ) {
			return dbGetMusic();
		}
		else {
			addMessage( -1, "Database %s and backup are corrupt!\nRun 'mixplay -C' to rescan", getConfig()->dbname );
		}
	}

	addMessage( 1, "Loaded %i titles from the database", index-1 );

	return ( dbroot?dbroot->next:NULL );
}

/**
 * checks for removed entries in the database
 * i.e. titles that are in the database but no longer on the medium
 */
int dbCheckExist( void ) {
	mptitle_t *root;
	mptitle_t *runner;
	int num=0;

	root=getConfig()->root;
	if( root == NULL ) {
		addMessage( -1, "No music in database!" );
		return -1;
	}
	runner=root;
	addMessage( 1, "Cleaning database..." );

	do {
		activity( 1, "Cleaning" );

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

	if( runner == NULL ) {
		getConfig()->root=NULL;
	}
	else if( num > 0 ) {
		dbMarkDirty();
	}

	return num;
}

static int dbAddTitle( int db, mptitle_t *title ) {
    dbentry_t dbentry;

    assert( 0 != db );

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
int dbAddTitles( char *basedir ) {
	mptitle_t *fsroot;
	mptitle_t *fsnext;
	mptitle_t *dbroot;
	mptitle_t *dbrunner;
	unsigned count=0, mean=0;
	unsigned index=0;
	int num=0, db=0;

	dbroot=getConfig()->root;
	if( dbroot == NULL ) {
		addMessage( 0, "No database loaded!" );
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
		if( count > 0 ){
			mean=(mean / count);
		}
		else {
			addMessage(-1, "No playable titles available!");
		}
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

	db=dbOpen( );
	if( db == -1 ) {
		getConfig()->root=wipeTitles(getConfig()->root);
		return -1;
	}

	while( NULL != fsroot ) {
		activity( 1, "Adding" );
		dbrunner = findTitle( dbroot, fsroot->path );

		if( NULL == dbrunner ) {
			fsnext=fsroot->next;
			/* This was the last title in fsroot */
			if( fsnext == fsroot ) {
				fsnext=NULL;
			}

			fsroot->playcount=mean;
			fsroot->key=index++;
			addMessage( 1, "Adding %s", fsroot->display );
			dbAddTitle( db, fsroot );

			/* unlink title from fsroot */
			fsroot->prev->next=fsroot->next;
			fsroot->next->prev=fsroot->prev;

			/* add title to dbroot */
			if( getFavplay() ) {
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
		addMessage( 0, "Setting new active database" );
		getConfig()->root=dbroot;
	}

	return num;
}

static int checkPath( mptitle_t *entry, int range ) {
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

/**
 * This should probably move to musicmgr..
 */
int dbNameCheck( void ) {
	mptitle_t *root;
	mptitle_t *currentEntry;
	mptitle_t *runner;
	int				count=0;
	int				qcnt=0;
	FILE 			*fp;
	int				match;
	char			rmpath[MAXPATHLEN+1];

	/* not using the live database as we need the marker */
	root=dbGetMusic( );
	if( root == NULL ) {
		addMessage( -1, "No music in database!");
		return -1;
	}

	snprintf( rmpath, MAXPATHLEN, "%s/.mixplay/rmlist.sh", getenv("HOME"));
	fp=fopen( rmpath, "w" );
	if( NULL == fp ) {
		addMessage( -1, "Could not open %s for writing!", rmpath );
		return -1;
	}

	/* start with a clean list, old doublets may have been deleted by now */
	getConfig()->dbllist=wipeList(getConfig()->dbllist);
	getListPath( rmpath, mpc_doublets );
	unlink(rmpath);

	fprintf( fp, "#!/bin/bash\n" );

	currentEntry=root;
	while( currentEntry->next != root ) {
		activity( 1, "Namechecking" );
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
							handleDBL(currentEntry);
							addMessage( 1, "Marked %s", currentEntry->path );
							fprintf( fp, "## Original at %s\n", runner->path );
							fprintf( fp, "rm \"%s\" >> %s/.mixplay/mixplay.dbl\n\n",
								currentEntry->path, getenv("HOME"));
							runner->flags |= MP_MARK;
							count++;
							break;
						case  4: /* 0100 */
						case  8: /* 1000 */
						case 12: /* 1100 */
						case 13: /* 1101 */
						case 14: /* 1110 */
							handleDBL(runner);
							addMessage( 1, "Marked %s", runner->path );
							fprintf( fp, "## Original at %s\n", currentEntry->path );
							fprintf( fp, "rm \"%s\" >> %s/.mixplay/mixplay.dbl\n\n",
								runner->path, getenv("HOME"));
							currentEntry->flags |= MP_MARK;
							count++;
							break;
						case  0: /* 0000 */
						case  5: /* 0101 */
						case  6: /* 0110 */
						case  9: /* 1001 */
						case 10: /* 1010 */
							fprintf( fp, "## Uncertain match! Either:\n" );
							fprintf( fp, "#rm \"%s\"\n", currentEntry->path );
							fprintf( fp, "#echo \"%s\" >>  %s/.mixplay/mixplay.dbl\n",
								currentEntry->path, getenv("HOME") );
							fprintf( fp, "## Or:\n" );
							fprintf( fp, "#rm \"%s\"\n", runner->path );
							fprintf( fp, "#echo \"%s\" >>  %s/.mixplay/mixplay.dbl\n\n",
								runner->path, getenv("HOME") );
							runner->flags |= MP_MARK; /* make sure only one of the doublets is used for future checkings */
							qcnt++;
							break;
						case 15: /* 1111 */
							/* both titles are fine! */
							break;
						default:
							addMessage( 0, "Incorrect match: %i", match );
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

	if( qcnt > 0 ) {
		fprintf( fp, "echo \"Remember to clean the database!\"\n" );
		addMessage( 0, "Found %i questionable titles", qcnt );
		addMessage( 0, "Check rmlist.sh in config dir" );
	}
	fclose( fp );
	wipeTitles(root);

	return count;
}

/**
 * Creates a backup of the current database file and dumps the
 * current reindexed database in a new file
 */
void dbWrite( void ) {
	int db;
	unsigned int index=1;
	const char *dbname=getConfig()->dbname;
	mptitle_t *root=getConfig()->root;
	mptitle_t *runner=root;

	if( root == NULL ) {
		addMessage(1, "Trying to save database in play/stream mode!");
		getConfig()->dbDirty=0;
		return;
	}

	fileBackup( dbname );
	db=dbOpen( dbname );
	if( db == -1 ) {
		return;
	}

	do {
		runner->key=index;
		dbPutTitle( db, runner );
		index++;
		runner=runner->next;
	}
	while( runner != root );

	dbClose( db );
	getConfig()->dbDirty=0;
}
