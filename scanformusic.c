#include "dbutils.h"
#include "musicmgr.h"

/**
 * print out CLI usage
 */
static void usage( char *progname ){
	printf( "%s - read Music directory into mixplay database\n", progname );
	printf( "Usage: %s [-v] [-d <dbname>] [source]\n", progname );
	printf( "[source]    : path do music directory [.]\n" );
	printf( "-d <dbname> : name of database [mixplay.db]\n" );
	printf( "-v          : increase Verbosity\n" );
	printf( "-C          : create new database\n");
	printf( "-A          : add new titles\n");
	printf( "-D          : remove titles that are no longer on the filesystem\n");
	exit(0);
}

/**
 * 	searches for a given path in the mixplay entry list
 */
static struct entry_t *findTitle( struct entry_t *base, const char *path ) {
	struct entry_t *runner;
	if( NULL == base ) return NULL;

	runner=base;
	do {
		if( strstr( runner->path, path ) ) return runner;
		runner=runner->next;
	} while ( runner != base );

	return NULL;
}

int main( int argc, char **argv ){
	struct entry_t *fsroot=NULL;
	struct entry_t *dbroot=NULL;
	struct entry_t *guard;
	struct entry_t *dbrunner;

	char dirbuf[MAXPATHLEN];
	char basedir[MAXPATHLEN];
	char dbname[MAXPATHLEN];
	char line[MAXPATHLEN];
	char *b;
	char c;
	int db=0;
	FILE *fp;
	int num=0;
	int clear=0, add=0, rem=0;

	b=getenv("HOME");
	sprintf( dirbuf, "%s/.mixplay", b );
	fp=fopen(dirbuf, "r");
	if( NULL != fp ) {
		do {
			fgets( line, MAXPATHLEN, fp );
			if( strlen(line) > 2 ) {
				line[strlen(line)-1]=0;
				switch( line[0] ) {
				case 'd':
					strncpy( dbname, line+1, MAXPATHLEN );
					break;
				case 's':
					strncpy( basedir, line+1, MAXPATHLEN );
					break;
				// Ignore everything else
				case 'b':
				case 'w':
				case '+':
				case '-':
				case '#':
					break;
				default:
					fail( "Config error:", line, -1 );
					break;
				}
			}
		} while( !feof(fp) );
	}
	else {
		printf( "%s does not exist.\n", dirbuf );
	}

	if( 0 == strlen(basedir) ) {
		if (NULL == getcwd(basedir, MAXPATHLEN))
			fail("Could not get current dir!", "", errno);
	}

	while ((c = getopt(argc, argv, "vd:CAD")) != -1) {
		switch (c) {
		case 'v':
			incVerbosity();
		break;
		case 'd':
			strncpy( dbname, optarg, MAXPATHLEN );
			break;
		case 'C':
			clear=1;
			break;
		case 'A':
			add=1;
			break;
		case 'D':
			rem=1;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (optind < argc) {
		strncpy( basedir, argv[optind], MAXPATHLEN );
	}

	if( ( 0 != strlen(basedir) ) &&  ( -1 == chdir( basedir ) ) ) {
		fail("Cannot access basedir", basedir, errno );
	}

	if (NULL == getcwd(basedir, MAXPATHLEN))
		fail("Could not get current dir!", "", errno);

	if( 0 == strlen(dbname) ) {
		strncpy( dbname, "mixplay.db", MAXPATHLEN );
	}

	if( clear ) {
		// delete database so the next dbOpen will create a new instance
		if( 0 != remove(dbname) ) {
			fail("Cannot delete", dbname, errno );
		}
	}

	dbOpen( &db, dbname );
	dbroot=dbGetMusic( db );

	dbrunner=dbroot;

	// add all titles to the database
	// existing titles will be ignored to keep MP3 tag info
	// and favourite marks
	if( add ) {
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
	}

	// check for titles in the database which are no
	// longer on the filesystem
	if( rem ) {
		guard=dbroot;

		do {
			activity( "Cleaning" );
			if( !mp3Exists(dbrunner) ) {
				if(guard == dbrunner) guard=dbrunner->prev;
				dbrunner=removeTitle( dbrunner );
				num++;
			}
			dbrunner=dbrunner->next;
		} while( guard != dbrunner );

		if( num > 0 ) {
			dbClose(&db);
			if( 0 != remove(dbname) ) {
				fail("Cannot delete", dbname, errno );
			}
			dbOpen(&db,dbname);
			while( NULL != dbrunner ) {
				dbAddTitle( db, dbrunner );
				dbrunner=removeTitle(dbrunner);
			}
			printf("Removed %i titles\n", num );
		}
		else {
			wipeTitles(dbroot);
			printf("No titles to remove\n" );
		}
	}

	dbClose(&db);
	return 0;
}
