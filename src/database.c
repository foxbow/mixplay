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
static void dbClose(int32_t db) {
	close(db);
	return;
}

void dbMarkDirty(void) {
	/* is there a database in use at all? */
	if (!(getConfig()->mpmode & PM_DATABASE)) {
		return;
	}
	/* ignore changes to the database in favplay mode */
	if (getFavplay()) {
		return;
	}

	if (getConfig()->dbDirty++ > 25) {
		dbWrite(0);
	}
}

mptitle_t *getTitleByIndex(uint32_t index) {
	mptitle_t *root = getConfig()->root;
	mptitle_t *run = root;

	if ((root == NULL) || (index == 0)) {
		return NULL;
	}

	do {
		if (run->key == index) {
			return run;
		}
		run = run->next;
	} while (root != run);

	return NULL;
}

/**
 * searches for a title that fits the name in the range.
 * This is kind of a hack to turn an artist name or an album name into
 * a title that can be used for FAV/DNP marking.
 */
mptitle_t *getTitleForRange(const mpcmd_t range, const char *name) {
	/* start with the current title so we can notice it disappear on DNP */
	mptitle_t *root = getConfig()->current->title;
	mptitle_t *run = root;

	if (root == NULL) {
		return NULL;
	}

	if (!MPC_EQALBUM(range) && !MPC_EQARTIST(range)) {
		addMessage(0,
				   "Can only search represantatives for Artists and Albums!");
		return NULL;
	}

	do {
		if (MPC_EQALBUM(range) && !strcasecmp(run->album, name)) {
			return run;
		}
		if (MPC_EQARTIST(range) && !strcasecmp(run->artist, name)) {
			return run;
		}
		run = run->next;
	} while (root != run);

	return NULL;
}

/* compares two strings backwards.
   returns 0 on inequality
           1 on equality
   used to compare paths since the difference is more likely to be at the end
   so the difference will we noted faster and with more than 5k titles things
   like these start to matter */
static int32_t strreq(const char *str1, const char *str2) {
	size_t len = strlen(str1);

	if (strlen(str2) != len) {
		return 0;
	}

	do {
		if (str1[len] != str2[len]) {
			return 0;
		}
	} while (--len != 0);

	return 1;
}

/**
 * 	searches for a given path in the mixplay entry list
 */
static mptitle_t *findTitle(mptitle_t * base, const char *path) {
	mptitle_t *runner;

	if (NULL == base) {
		return NULL;
	}

	runner = base;

	do {
		if (strreq(runner->path, path)) {
			return runner;
		}

		runner = runner->next;
	}
	while (runner != base);

	return NULL;
}

/**
 * checks if a given title still exists on the filesystem
 */
int32_t mp3Exists(const mptitle_t * title) {
	return (access(fullpath(title->path), F_OK) == 0);
}

/**
 * deletes an entry from the database list
 * This should only be used on a database cleanup!
 */
static mptitle_t *removeTitle(mptitle_t * entry) {
	mptitle_t *next = NULL;

	if (entry->next != entry) {
		next = entry->next;
		entry->next->prev = entry->prev;
		entry->prev->next = entry->next;
	}

	remFromPLByKey(entry->key);

	free(entry);
	return next;
}


/**
 * turn a database entry into a mixplay structure
 */
static void db2entry(dbentry_t * dbentry, mptitle_t * entry) {
	memset(entry, 0, ESIZE);
	strcpy(entry->path, dbentry->path);
	strcpy(entry->artist, dbentry->artist);
	strcpy(entry->title, dbentry->title);
	strcpy(entry->album, dbentry->album);
	strcpy(entry->genre, dbentry->genre);
	strtcpy(entry->display, dbentry->artist, MAXPATHLEN - 1);
	strtcat(entry->display, " - ", MAXPATHLEN - 1);
	strtcat(entry->display, dbentry->title, MAXPATHLEN - 1);
	entry->playcount = dbentry->playcount;
	entry->skipcount = dbentry->skipcount;
	entry->favpcount = dbentry->playcount;
	entry->flags = 0;
}

/**
 * pack a mixplay entry into a database structure
 */
static void entry2db(mptitle_t * entry, dbentry_t * dbentry) {
	memset(dbentry, 0, DBESIZE);
	strcpy(dbentry->path, entry->path);
	strcpy(dbentry->artist, entry->artist);
	strcpy(dbentry->title, entry->title);
	strcpy(dbentry->album, entry->album);
	strcpy(dbentry->genre, entry->genre);
	dbentry->playcount = entry->playcount;
	dbentry->skipcount = entry->skipcount;
}

/**
 * opens the database file
 */
static int32_t dbOpen(void) {
	int32_t db = -1;
	char *path = getConfig()->dbname;

	db = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

	if (-1 == db) {
		addMessage(-1, "Could not open database %s", path);
	}
	else {
		addMessage(2, "Opened database %s", path);
	}
	return db;
}

/**
 * adds/overwrites a title to/in the database
 */
static int32_t dbPutTitle(int32_t db, mptitle_t * title) {
	dbentry_t dbentry;

	assert(0 != db);

	if (0 == title->key) {
		lseek(db, 0, SEEK_END);
	}
	else {
		if (-1 == lseek(db, DBESIZE * (title->key - 1), SEEK_SET)) {
			fail(errno, "Could not skip to title %s", title->path);
		}
	}

	entry2db(title, &dbentry);

	if (write(db, &dbentry, DBESIZE) != DBESIZE) {
		fail(errno, "Could not write entry %s!", title->path);
	}

	return 0;
}

/**
 * takes a database entry and adds it to a mixplay entry list
 * if there is no list, a new one will be created
 */
static mptitle_t *addDBTitle(dbentry_t * dbentry, mptitle_t * root,
							 uint32_t index) {
	mptitle_t *entry;

	entry = (mptitle_t *) falloc(1, sizeof (mptitle_t));

	db2entry(dbentry, entry);
	entry->key = index;

	if (NULL == root) {
		root = entry;
		root->prev = root;
		root->next = root;
	}
	else {
		entry->next = root->next;
		entry->prev = root;
		root->next->prev = entry;
		root->next = entry;
	}

	return entry;
}

/**
 * gets all titles from the database and returns them as a mixplay entry list
 */
mptitle_t *dbGetMusic() {
	dbentry_t dbentry;
	uint32_t index = 1;			/* index 0 is reserved for titles not in the db! */
	mptitle_t *dbroot = NULL;
	int32_t db;
	size_t len;

	db = dbOpen();
	if (db == -1) {
		return NULL;
	}

	activity(1, "Loading database");
	while ((len = read(db, &dbentry, DBESIZE)) == DBESIZE) {
		/* explicitly terminate strings. Those should never ever be not terminated,
		 * but it may make a change on reading a corrupted database */
		dbentry.path[MAXPATHLEN - 1] = 0;
		dbentry.artist[NAMELEN - 1] = 0;
		dbentry.title[NAMELEN - 1] = 0;
		dbentry.album[NAMELEN - 1] = 0;
		dbentry.genre[NAMELEN - 1] = 0;

		/* support old database title path format */
		if ((dbentry.path[0] == '/') &&
			(strstr(dbentry.path, getConfig()->musicdir) != dbentry.path)) {
			strtcpy(dbentry.path, fullpath(&(dbentry.path[1])), MAXPATHLEN);
			/* Do not use dbMarkDirty() here as that may trigger a write
			 * on the open database! */
			getConfig()->dbDirty = 1;
		}
		dbroot = addDBTitle(&dbentry, dbroot, index);
		index++;
	}

	dbClose(db);

	if (0 != len) {
		addMessage(0, "Database is corrupt, trying backup.");
		dbroot = wipeTitles(dbroot);
		if (!fileRevert(getConfig()->dbname)) {
			return dbGetMusic();
		}
		else {
			/* Maybe make an UI scan possible or trigger it here explicitly */
			addMessage(-1,
					   "Database %s and backup are corrupt!\nRun 'mixplay -C' to rescan",
					   getConfig()->dbname);
		}
	}

	addMessage(1, "Loaded %i titles from the database", index - 1);

	return (dbroot ? dbroot->next : NULL);
}

/**
 * checks for removed entries in the database
 * i.e. titles that are in the database but no longer on the medium
 */
int32_t dbCheckExist(void) {
	mptitle_t *root;
	mptitle_t *runner;
	int32_t num = 0;

	root = getConfig()->root;
	if (root == NULL) {
		addMessage(-1, "No music in database!");
		return -1;
	}
	runner = root;
	addMessage(0, "Cleaning database");
	do {
		if (!mp3Exists(runner)) {
			if (root == runner) {
				root = runner->prev;
			}

			runner = removeTitle(runner);
			num++;
		}
		else {
			runner = runner->next;
		}
	}
	while ((runner != NULL) && (root != runner));

	if (runner == NULL) {
		getConfig()->root = NULL;
	}
	else if (num > 0) {
		dbMarkDirty();
	}

	return num;
}

static int32_t dbAddTitle(int32_t db, mptitle_t * title) {
	dbentry_t dbentry;

	assert(0 != db);

	lseek(db, 0, SEEK_END);

	entry2db(title, &dbentry);

	if (write(db, &dbentry, DBESIZE) != DBESIZE) {
		fail(errno, "Could not write entry %s!", title->path);
	}

	return 0;
}

/**
 * the name is misleading, it just is chosen to match addNewPath()
 * where it is used
 */
void dbAddPath(mptitle_t * title) {
	int db = dbOpen();

	if (db == -1) {
		fail(-1, "Database is not available!");
		return;
	}
	dbAddTitle(db, title);
	dbClose(db);
}

/**
 * make up a playcount for new titles to be added to the database
 * Right now it's the max playcount - 1 or 0
 */
uint32_t getNewPlaycount() {
	uint32_t mean = 0;
	mptitle_t *dbrunner = getConfig()->root;

	if (dbrunner == NULL)
		return 0;

	do {
		/* find mean playcount */
		if (!(dbrunner->flags & (MP_DNP | MP_DBL))
			&& (mean < dbrunner->playcount)) {
			mean = dbrunner->playcount;
		}
		dbrunner = dbrunner->next;
	}
	while (dbrunner != getConfig()->root);

	return mean > 0 ? mean - 1 : mean;
}

/**
 * adds new titles to the database
 * the new titles will have a playcount set to blend into the mix
 */
int32_t dbAddTitles(char *basedir) {
	mptitle_t *fsroot;
	mptitle_t *fsnext;
	mptitle_t *dbroot;
	mptitle_t *dbrunner;
	uint32_t mean = 0;
	uint32_t index = 0;
	int32_t num = 0, db = 0;

	dbroot = getConfig()->root;
	if (dbroot == NULL) {
		addMessage(0, "No database loaded!");
	}
	else {
		addMessage(0, "Adding new titles");
		dbrunner = dbroot;

		index = dbroot->prev->key;
		mean = getNewPlaycount();
	}

	addMessage(0, "Using mean playcount %d", mean);
	addMessage(0, "%d titles in current database", index);
	index++;

	/* scan directory */
	addMessage(0, "Scanning...");
	fsroot = recurse(basedir, NULL);
	if (fsroot == NULL) {
		addMessage(-1, "No music found in %s!", basedir);
		return 0;
	}

	fsroot = fsroot->next;

	addMessage(0, "Adding titles...");

	db = dbOpen();
	if (db == -1) {
		getConfig()->root = wipeTitles(getConfig()->root);
		return -1;
	}

	while (NULL != fsroot) {
		dbrunner = findTitle(dbroot, fsroot->path);

		if (NULL == dbrunner) {
			fsnext = fsroot->next;
			/* This was the last title in fsroot */
			if (fsnext == fsroot) {
				fsnext = NULL;
			}

			fsroot->playcount = mean;
			fsroot->favpcount = mean;
			fsroot->key = index++;
			addMessage(1, "Adding %s", fsroot->display);
			dbAddTitle(db, fsroot);

			/* unlink title from fsroot */
			fsroot->prev->next = fsroot->next;
			fsroot->next->prev = fsroot->prev;

			/* add title to dbroot */
			if (dbroot == NULL) {
				dbroot = fsroot;
				dbroot->prev = dbroot;
				dbroot->next = dbroot;
			}
			else {
				dbroot->prev->next = fsroot;
				fsroot->prev = dbroot->prev;
				fsroot->next = dbroot;
				dbroot->prev = fsroot;
			}
			num++;

			fsroot = fsnext;
		}
		else {
			fsroot = removeTitle(fsroot);
		}
	}
	dbClose(db);

	if (getConfig()->root == NULL) {
		addMessage(0, "Setting new active database");
		getConfig()->root = dbroot;
	}

	return num;
}

/**
 * check if the 'range' part of the title is part of the path
 * to the actual file. This is used to check if the title is
 * part of a dedicated album or just floating in a mix dir
 * or a sampler.
 */
static int32_t checkPath(mptitle_t * entry, int32_t range) {
	char path[MAXPATHLEN];
	char check[NAMELEN] = "";
	char *pos;

	strltcpy(path, entry->path, MAXPATHLEN);
	pos = strrchr(path, '/');
	if (NULL != pos) {
		*pos = 0;
	}

	switch (range) {
	case mpc_artist:
		strltcpy(check, entry->artist, NAMELEN);
		break;
	case mpc_album:
		strltcpy(check, entry->artist, NAMELEN);
		break;
	default:
		fail(F_FAIL, "checkPath() range %i not implemented!\n", range);
		break;
	}
	return (strstr(path, check) != NULL);
}

/**
 * This should probably move to musicmgr..
 */
int32_t dbNameCheck(void) {
	mptitle_t *root;
	mptitle_t *currentEntry;
	mptitle_t *runner;
	int32_t count = 0;
	int32_t qcnt = 0;
	FILE *fp;
	int32_t match;
	char rmpath[MAXPATHLEN + 1];

	/* not using the live database as we need the marker */
	/* TODO: this is no longer true, check! */
	root = dbGetMusic();
	if (root == NULL) {
		addMessage(-1, "No music in database!");
		return -1;
	}

	snprintf(rmpath, MAXPATHLEN, "%s/.mixplay/rmlist.sh", getenv("HOME"));
	fp = fopen(rmpath, "w");
	if (NULL == fp) {
		addMessage(-1, "Could not open %s for writing!", rmpath);
		return -1;
	}

	addMessage(0, "Namechecking");
	/* start with a clean list, old doublets may have been deleted by now */
	getConfig()->dbllist = wipeList(getConfig()->dbllist);
	getListPath(mpc_doublets, rmpath);
	unlink(rmpath);

	fprintf(fp, "#!/bin/bash\n");

	currentEntry = root;
	while (currentEntry->next != root) {
		if (!(currentEntry->flags & MP_MARK)) {
			runner = currentEntry->next;
			do {
				if (!(runner->flags & MP_MARK)) {
					/* if the titles identify the same, check their paths */
					if (strcmp(runner->display, currentEntry->display) == 0) {
						match = 0;
						if (checkPath(runner, mpc_artist)) {
							match |= 1;
						}
						if (checkPath(runner, mpc_album)) {
							match |= 2;
						}
						if (checkPath(currentEntry, mpc_artist)) {
							match |= 4;
						}
						if (checkPath(currentEntry, mpc_album)) {
							match |= 8;
						}

						switch (match) {
						case 1:	/* 0001 */
						case 2:	/* 0010 */
						case 3:	/* 0011 */
						case 7:	/* 0111 */
						case 11:	/* 1011 */
							/* runner is in an album, currentEntry in a mix */
							handleDBL(currentEntry);
							addMessage(1, "Marked %s", currentEntry->path);
							fprintf(fp, "## Original at %s\n", runner->path);
							fprintf(fp,
									"rm \"%s\" >> %s/.mixplay/mixplay.dbl\n\n",
									currentEntry->path, getenv("HOME"));
							runner->flags |= MP_MARK;
							count++;
							break;
						case 4:	/* 0100 */
						case 8:	/* 1000 */
						case 12:	/* 1100 */
						case 13:	/* 1101 */
						case 14:	/* 1110 */
							/* currentEntry is in an album, runner in a mix */
							handleDBL(runner);
							addMessage(1, "Marked %s", runner->path);
							fprintf(fp, "## Original at %s\n",
									currentEntry->path);
							fprintf(fp,
									"rm \"%s\" >> %s/.mixplay/mixplay.dbl\n\n",
									runner->path, getenv("HOME"));
							currentEntry->flags |= MP_MARK;
							count++;
							break;
						case 0:	/* 0000 */
						case 5:	/* 0101 */
						case 6:	/* 0110 */
						case 9:	/* 1001 */
						case 10:	/* 1010 */
							/* both seem to be in a sampler/mix */
							fprintf(fp, "## Uncertain match! Either:\n");
							fprintf(fp, "#rm \"%s\"\n", currentEntry->path);
							fprintf(fp,
									"#echo \"%s\" >>  %s/.mixplay/mixplay.dbl\n",
									currentEntry->path, getenv("HOME"));
							fprintf(fp, "## Or:\n");
							fprintf(fp, "#rm \"%s\"\n", runner->path);
							fprintf(fp,
									"#echo \"%s\" >>  %s/.mixplay/mixplay.dbl\n\n",
									runner->path, getenv("HOME"));
							runner->flags |= MP_MARK;	/* make sure only one of the doublets is used for future checkings */
							qcnt++;
							break;
						case 15:	/* 1111 */
							/* both titles are fine! */
							break;
						default:
							addMessage(0, "Incorrect match: %i", match);
							break;
						}
					}
				}
				if (currentEntry->flags & MP_MARK) {
					runner = root;
				}
				else {
					runner = runner->next;
				}
			} while (runner != root);
		}
		currentEntry = currentEntry->next;
	}

	if (qcnt > 0) {
		fprintf(fp, "echo \"Remember to clean the database!\"\n");
		addMessage(0, "Found %i questionable titles", qcnt);
		addMessage(0, "Check rmlist.sh in config dir");
	}
	fclose(fp);
	wipeTitles(root);

	return count;
}

/**
 * Creates a backup of the current database file and dumps the
 * current reindexed database in a new file
 *
 * if force is set, the database is written without checking the dbDirty
 * flag.
 */
void dbWrite(int32_t force) {
	int32_t db;
	uint32_t index = 1;
	mptitle_t *root = getConfig()->root;
	mptitle_t *runner = root;

	if (!force && (getConfig()->dbDirty == 0)) {
		addMessage(1, "No change in database.");
		return;
	}

	addMessage(1, "Saving database.");
	getConfig()->dbDirty = 0;

	if (root == NULL) {
		addMessage(0, "Trying to save database in play/stream mode!");
		return;
	}

	fileBackup(getConfig()->dbname);
	db = dbOpen();
	if (db == -1) {
		return;
	}

	do {
		runner->key = index;
		dbPutTitle(db, runner);
		index++;
		runner = runner->next;
	}
	while (runner != root);

	dbClose(db);
}
