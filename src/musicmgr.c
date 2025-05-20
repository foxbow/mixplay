/**
 * collection of functions to manage a list of titles
 */
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <limits.h>

#include "database.h"
#include "musicmgr.h"
#include "mpgutils.h"
#include "utils.h"

/* Not a #define as we need the reference later */
static char ARTIST_SAMPLER[] = "Various";

int32_t getListPath(mpcmd_t cmd, char path[MAXPATHLEN]) {
	if (getConfig()->active < 1) {
		return -1;
	}
	if (MPC_CMD(cmd) == mpc_doublets) {
		snprintf(path, MAXPATHLEN, "%s/.mixplay/mixplay.dbl", getenv("HOME"));
	}
	else {
		profile_t *profile = getProfile(getConfig()->active);

		if (profile != NULL) {
			return -1;
		}
		snprintf(path, MAXPATHLEN, "%s/.mixplay/%s.", getenv("HOME"),
				 profile->name);
		if (MPC_CMD(cmd) == mpc_fav) {
			strtcat(path, "fav", MAXPATHLEN);
		}
		else {
			strtcat(path, "dnp", MAXPATHLEN);
		}
	}
	return 0;
}

/**
 * add a line to a file
 */
static int32_t addToList(const char *line, mpcmd_t cmd) {
	FILE *fp;
	char path[MAXPATHLEN + 1];

	getListPath(cmd, path);

	fp = fopen(path, "a");
	if (NULL == fp) {
		addMessage(-1, "Could not open %s", path);
		return -1;
	}

	fputs(line, fp);
	fputc('\n', fp);
	fclose(fp);

	notifyChange(MPCOMM_LISTS);
	return 1;
}

mpplaylist_t *addPLDummy(mpplaylist_t * pl, const char *name) {
	mpplaylist_t *buf;
	mptitle_t *title = (mptitle_t *) falloc(1, sizeof (mptitle_t));

	if (pl == NULL) {
		pl = (mpplaylist_t *) falloc(1, sizeof (mpplaylist_t));
		pl->prev = NULL;
		pl->next = NULL;
	}
	else {
		buf = pl->prev;
		pl->prev = (mpplaylist_t *) falloc(1, sizeof (mpplaylist_t));
		pl->prev->prev = buf;
		pl->prev->next = pl;
		if (buf != NULL) {
			buf->next = pl->prev;
		}
		pl = pl->prev;
	}
	strip(title->display, name, MAXPATHLEN - 1);
	strip(title->title, name, NAMELEN - 1);

	pl->title = title;

	return pl;
}

/**
 * appends a title to the end of a playlist
 * if pl is NULL a new Playlist is created
 * this function either returns pl or the head of the new playlist
 */
static mpplaylist_t *appendToPL(mptitle_t * title, mpplaylist_t * pl,
								const int32_t mark) {
	mpplaylist_t *runner = pl;

	if (runner != NULL) {
		if (runner->title == NULL) {
			addMessage(-1, "Found an empty entry!");
		}
		while (runner->next != NULL) {
			runner = runner->next;
		}
		addToPL(title, runner, mark);
	}
	else {
		pl = addToPL(title, NULL, mark);
	}
	return pl;
}

/*
 * removes a title from the current playlist chain and returns the next title
 */
static mpplaylist_t *remFromPL(mpplaylist_t * pltitle) {
	mpplaylist_t *ret = pltitle->next;

	if (pltitle->prev != NULL) {
		pltitle->prev->next = pltitle->next;
	}
	if (pltitle->next != NULL) {
		pltitle->next->prev = pltitle->prev;
	}
	/* title is no longer in the playlist */
	pltitle->title->flags &= ~MP_INPL;

	free(pltitle);
	return ret;
}

/**
 * checks if the playcount needs to be increased and if the skipcount
 * needs to be decreased. In both cases the updated information is written
 * back into the db.
 * returns 1 if the title was marked DNP due to skipping
 */
int32_t playCount(mptitle_t * title, int32_t skip) {
	int32_t rv = 0;

	/* playcount only makes sense with a title list */
	if (getConfig()->mpmode & PM_STREAM) {
		addMessage(1, "No skip on streams!");
		return -1;
	}

	/* not marked = searchplay, does not count
	 * just marked dnp? someone else may like it though */
	if (!(title->flags & MP_INPL) || (title->flags & (MP_DNP | MP_DBL))) {
		addMessage(2, "%s is dnp or not marked -> ignored", title->display);
		return -1;
	}

	/* skipcount only happens on database play */
	if (getConfig()->mpmode & PM_DATABASE) {
		if (skip) {
			title->skipcount++;
			if (title->skipcount >= getConfig()->skipdnp) {
				title->skipcount = getConfig()->skipdnp;
				/* three strikes and you're out */
				title->skipcount--;
				addMessage(0, "Marked %s as DNP for skipping!",
						   title->display);
				if (!handleRangeCmd((mpcmd_t) (mpc_display | mpc_dnp), title)) {
					/* Can happen if the title has been marked as a favourite,
					 * so we have the case that the title is an explicit
					 * favourite AND was skipped three times... */
					addMessage(0, "%s is an explicit Favourite!",
							   title->display);
				}
				else {
					rv = 1;
				}
			}
			else {
				addMessage(1, "%s was skipped (%i/%i)!", title->display,
						   title->skipcount, getConfig()->skipdnp);
			}
			dbMarkDirty();
		}
		else if (title->skipcount > 0) {
			title->skipcount--;
			dbMarkDirty();
		}
	}

	if (getFavplay() ||
		((title->flags & MP_FAV) && (title->favpcount < title->playcount))) {
		title->favpcount++;
	}
	else {
		title->playcount++;
		if (!(title->flags & MP_FAV)) {
			title->favpcount++;
		}
		dbMarkDirty();
	}

	return rv;
}

/*
 * removes the title with the given key from the playlist
 * this returns NULL or a valid playlist anchor in case the current title was
 * removed
 */
mpplaylist_t *remFromPLByKey(const uint32_t key) {
	mpplaylist_t *root;
	mpplaylist_t *pl;

	if (getCurrent() == NULL) {
		addMessage(0, "Cannot remove titles from an empty playlist");
		return NULL;
	}

	if (key == 0) {
		return getCurrent();
	}

	lockPlaylist();
	root = getCurrent();
	pl = root;

	while (pl->prev != NULL) {
		pl = pl->prev;
	}

	while ((pl != NULL) && (pl->title->key != key)) {
		pl = pl->next;
	}

	if (pl != NULL) {
		if (pl->prev != NULL) {
			pl->prev->next = pl->next;
		}
		if (pl->next != NULL) {
			pl->next->prev = pl->prev;
		}
		if (pl == root) {
			if (pl->next != NULL) {
				root = pl->next;
			}
			else if (pl->prev != NULL) {
				root = pl->prev;
			}
			else {
				root = NULL;
			}
		}
		free(pl);
		pl = NULL;
	}
	else {
		addMessage(0, "No title with key %i in playlist!", key);
	}
	unlockPlaylist();
	return root;
}

/**
 * inserts a title into the playlist chain. Creates a new playlist
 * if no target is set.
 * This function returns a valid target
 * if mark is true, the title will be checked and marked for playing in
 * default mixplay, otherwise it is a searched title and will be
 * played out of order or added to a playlist result.
 */
mpplaylist_t *addToPL(mptitle_t * title, mpplaylist_t * target,
					  const int32_t mark) {
	mpplaylist_t *buf = NULL;

	if (mark && (title->flags & MP_INPL)) {
		addMessage(0, "Trying to add %s twice! (%i)", title->display,
				   title->flags);
	}

	buf = (mpplaylist_t *) falloc(1, sizeof (mpplaylist_t));
	memset(buf, 0, sizeof (mpplaylist_t));
	buf->title = title;

	if (target != NULL) {
		if (target->next != NULL) {
			target->next->prev = buf;
		}
		buf->next = target->next;
		target->next = buf;
		buf->prev = target;
	}
	target = buf;

	if (mark) {
		title->flags |= MP_INPL;
	}
	/* do not notify here as the playlist may be a search result */
	return target;
}

/**
 * helperfunction for scandir() - just return unhidden directories and links
 */
static int32_t dsel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			((entry->d_type == DT_DIR) || (entry->d_type == DT_LNK)));
}

/**
 * helperfunction for scandir() - just return unhidden regular music files
 */
static int32_t msel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			(entry->d_type == DT_REG) && isMusic(entry->d_name));
}

/**
 * helperfunction for scandir() - just return unhidden regular playlist files
 */
static int32_t plsel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			(entry->d_type == DT_REG) && endsWith(entry->d_name, ".m3u"));
}

#define isnum(x) (((x)>'0') && ((x)<'9'))
/**
 * helperfunction for scandir() sorts entries numerically first then
 * alphabetical
 */
static int32_t tsort(const struct dirent **d1, const struct dirent **d2) {
	int32_t res = 0;

	/* only do a numerical compare if both titles start with a number */
	if (isnum((*d1)->d_name[0]) && isnum((*d2)->d_name[0])) {
		res = atoi((*d1)->d_name) - atoi((*d2)->d_name);
	}
	if (res == 0) {
		res = strcasecmp((*d1)->d_name, (*d2)->d_name);
	}
	return res;
}

#undef isnum

/**
 * loads all playlists in cd into pllist
 */
int32_t getPlaylists(const char *cd, struct dirent ***pllist) {
	return scandir(cd, pllist, plsel, tsort);
}

/**
 * loads all music files in cd into musiclist
 */
static int32_t getMusic(const char *cd, struct dirent ***musiclist) {
	return scandir(cd, musiclist, msel, tsort);
}

/**
 * loads all directories in cd into dirlist
 */
static int32_t getDirs(const char *cd, struct dirent ***dirlist) {
	return scandir(cd, dirlist, dsel, alphasort);
}

/**
 * compare two strings and ignore the case.
 * returns 1 on equality and 0 on differerences.
 * This is not equal to stricmp/strcasecmp!
 */
static int32_t strieq(const char *t1, const char *t2) {
	size_t len = strlen(t1);

	if (len != strlen(t2))
		return 0;
	do {
		if (tolower(t1[len]) != tolower(t2[len]))
			return 0;
	} while (len-- > 0);
	return 1;
}

/*
 * matches term with pattern in search.
 */
static int32_t isMatch(const char *term, const char *pat, const mpcmd_t range) {
	char loterm[MAXPATHLEN];

	if (MPC_ISFUZZY(range)) {
		strltcpy(loterm, term, MAXPATHLEN);
		return patMatch(loterm, pat);
	}

	/* mpc_substr is only sent from the UI */
	if (MPC_ISSUBSTR(range)) {
		strltcpy(loterm, term, MAXPATHLEN);
		return (strstr(loterm, pat) != NULL);
	}

	return strieq(term, pat);
}

/*
 * checks if a title entry 'title' matches the search term 'pat'
 * the test is driven by the first two characters in the
 * search term. The first character gives the range (talgd(p))
 * the second character notes if the search should be
 * exact or fuzzy (=*)
 *
 * todo: consider adding a exact substring match or deprecate
 *       fuzzy matching for fav/dnp
 */
static uint32_t matchTitle(mptitle_t * title, const char *pat) {
	mpcmd_t fuzzy = mpc_unset;
	int32_t res = 0;

	if (('=' == pat[1]) || ('*' == pat[1])) {
		if ('*' == pat[1]) {
			fuzzy = mpc_fuzzy;
		}

		switch (pat[0]) {
		case 't':
			if (isMatch(title->title, pat + 2, fuzzy))
				res = mpc_title;
			break;

		case 'a':
			if (isMatch(title->artist, pat + 2, fuzzy))
				res = mpc_artist;
			break;

		case 'l':
			if (isMatch(title->album, pat + 2, fuzzy))
				res = mpc_album;
			break;

		case 'g':
			if (isMatch(title->genre, pat + 2, fuzzy))
				res = mpc_genre;
			break;

		case 'd':
			if (isMatch(title->display, pat + 2, fuzzy))
				res = mpc_display;
			break;

		case 'p':
			if (isMatch(title->path, pat + 2, fuzzy))
				res = MPC_DFALL;
			break;

		default:
			addMessage(0, "Unknown range %c in %s!", pat[0], pat);
			break;
		}
	}
	else {
		addMessage(0, "Pattern without range: %s", pat);
		if (isMatch(title->display, pat, mpc_unset))
			res = mpc_display;
	}

	return res;
}

/**
 * fills the global searchresult structure with the results of the given search.
 * Returns the number of found titles.
 * pat - pattern to search for
 * range - search range
 */
int32_t search(const mpcmd_t range, const char *pat) {
	mpconfig_t *control = getConfig();
	mptitle_t *root = control->root;
	mptitle_t *runner = root;
	searchresults_t *res = control->found;
	uint32_t i = 0;
	uint32_t valid = 0;
	uint32_t found = 0;
	char *lopat;

	/* enter while the last result has not been sent yet! */
	assert(res->state != mpsearch_done);
	assert(pat != NULL);

	/* free buffer playlist, the arrays will not get lost due to the realloc later */
	wipeSearchList(control);
	res->tnum = 0;
	res->anum = 0;
	res->lnum = 0;

	if (root == NULL) {
		addMessage(-1, "No database loaded.");
		return 0;
	}

	/* whatever pattern we get, ignore case */
	lopat = toLower(strdup(pat));

	do {
		found = 0;

		/* ugly but at least somewhat understandable how titles get filtered */
		if (getFavplay()) {
			valid = runner->flags & MP_FAV;
		}
		else {
			valid = !(runner->flags & (MP_DNP | MP_DBL));
		}
		if (getConfig()->searchDNP) {
			valid = !valid;
		}

		if (valid) {
			/* check for searchrange and patterns */
			if (MPC_ISTITLE(range) && isMatch(runner->title, lopat, range)) {
				found |= mpc_title;
			}

			/* from a result point of view display(, path) and title are the same */
			if (MPC_ISDISPLAY(range) && isMatch(runner->display, lopat, range)) {
				found |= mpc_title;
			}

			if (MPC_ISARTIST(range) && isMatch(runner->artist, lopat, range)) {
				found |= mpc_artist;

				/* Add albums and titles if search was for artists only */
				if (MPC_EQARTIST(range)) {
					found |= mpc_title | mpc_album;
				}
			}

			if (MPC_ISALBUM(range) && isMatch(runner->album, lopat, range)) {
				found |= mpc_album;

				/* Add titles if search was for albums only */
				if (MPC_EQALBUM(range)) {
					found |= mpc_title;
				}
			}

			/* now interpret the value of 'found' */

			if (MPC_ISARTIST(found)) {
				/* check for new artist */
				for (i = 0; (i < res->anum)
					 && strcmp(res->artists[i], runner->artist); i++);
				if (i == res->anum) {
					res->anum++;
					res->artists =
						(char **) frealloc(res->artists,
										   res->anum * sizeof (char *));
					res->artists[i] = runner->artist;
				}
			}

			if (MPC_ISALBUM(found)) {
				/* check for new albums */
				for (i = 0;
					 (i < res->lnum) && !strieq(res->albums[i], runner->album);
					 i++);
				if (i == res->lnum) {
					/* album not yet in list */
					res->lnum++;
					res->albums =
						(char **) frealloc(res->albums,
										   res->lnum * sizeof (char *));
					res->albums[i] = runner->album;
					res->albart =
						(char **) frealloc(res->albart,
										   res->lnum * sizeof (char *));
					res->albart[i] = runner->artist;
				}
				/* fuzzy comparation to avoid collabs turning an album into a sampler */
				else if (!strieq(res->albart[i], ARTIST_SAMPLER) &&
						 !checkSim(res->albart[i], runner->artist)) {
					addMessage(1, "%s is considered a sampler (%s <> %s).",
							   runner->album, runner->artist, res->albart[i]);
					res->albart[i] = ARTIST_SAMPLER;
				}
			}

			if (MPC_ISTITLE(found) && (res->tnum++ < MAXSEARCH)) {
				res->titles = appendToPL(runner, res->titles, 0);
			}
		}
		runner = runner->next;
	} while (runner != root);

	/* result can be sent out now */
	res->state = mpsearch_done;

	free(lopat);
	return ((res->tnum > MAXSEARCH) ? -1 : (int32_t) res->tnum);
}

/**
 * applies the dnplist on a list of titles and marks matching titles
 * if the title is part of the playlist it will be removed from the playlist
 * too. This may lead to double played artists though...
 *
 * if dbl is true, then the title is marked as doublet as well
 *
 * returns the number of marked titles or -1 on error
 */
int32_t applyDNPlist(marklist_t * list, int32_t dbl) {
	mptitle_t *base = getConfig()->root;
	mptitle_t *pos = base;
	marklist_t *ptr = list;
	mpplaylist_t *pl = getCurrent();
	int32_t cnt = 0;
	uint32_t range = 0;

	if (NULL == list) {
		return 0;
	}

	if (dbl)
		activity(0, "Applying DBL list");
	else
		activity(0, "Applying DNP list");

	do {
		if (!(pos->flags & MP_DBL)) {
			ptr = list;

			while (ptr) {
				range = matchTitle(pos, ptr->dir);
				if (range > MPC_RANGE(pos->flags)) {
					addMessage(4, "[D] %s: %s", ptr->dir, pos->display);
					pos->flags = (range | MP_DNP);
					if (dbl)
						pos->flags = (MPC_DFRANGE | MP_DBL);
					cnt++;
					break;
				}
				ptr = ptr->next;
			}
		}
		pos = pos->next;
	}
	while (pos != base);

	if (pl != NULL) {
		while (pl->prev != NULL) {
			pl = pl->prev;
		}
	}

	while (pl != NULL) {
		if (pl->title->flags & (MP_DNP | MP_DBL)) {
			if (pl == getCurrent()) {
				getConfig()->current = pl->next;
			}
			pl = remFromPL(pl);
		}
		else {
			pl = pl->next;
		}
	}

	addMessage(1, "Marked %i titles as DNP", cnt);

	return cnt;
}

/**
 * This function sets the favourite bit on titles found in the given list
 */
static int32_t applyFAVlist(marklist_t * favourites) {
	marklist_t *ptr = NULL;
	mptitle_t *root = getConfig()->root;
	mptitle_t *runner = root;
	int32_t cnt = 0;
	uint32_t range = 0;

	if (NULL == root) {
		addMessage(0, "No music loaded for FAVlist");
		return -1;
	}

	if (favourites == NULL) {
		return 0;
	}

	activity(0, "Applying FAV list");

	do {
		if (!(runner->flags & MP_DBL)) {
			ptr = favourites;

			while (ptr) {
				range = matchTitle(runner, ptr->dir);
				if (range > MPC_RANGE(runner->flags)) {
					if (!(runner->flags & MP_FAV)) {
						addMessage(4, "[F] %s: %s", ptr->dir, runner->display);
						/* Save MP_INPL */
						runner->flags =
							(runner->flags & MP_INPL) | MP_FAV | range;
						cnt++;
					}
					ptr = NULL;
				}
				else {
					ptr = ptr->next;
				}
			}
		}
		runner = runner->next;
	} while (runner != root);

	addMessage(1, "Marked %i favourites", cnt);

	return cnt;
}

/* reset the given flags on all titles */
static void unsetFlags(uint32_t flags) {
	mptitle_t *guard = getConfig()->root;
	mptitle_t *runner = guard;

	do {
		runner->flags &= ~flags;
		runner = runner->next;
	} while (runner != guard);
}

void applyLists(int32_t clean) {
	mpconfig_t *control = getConfig();

	lockPlaylist();
	if (clean) {
		unsetFlags(MPC_DFRANGE | MP_FAV | MP_DNP);
	}
	applyFAVlist(control->favlist);
	applyDNPlist(control->dnplist, 0);
	unlockPlaylist();
	notifyChange(MPCOMM_LISTS);
}

/**
 * does the actual loading of a list
 */
marklist_t *loadList(const mpcmd_t cmd) {
	FILE *file = NULL;
	marklist_t *ptr = NULL;
	marklist_t *bwlist = NULL;
	char path[MAXPATHLEN];

	char *buff;
	int32_t cnt = 0;

	if (getListPath(cmd, path) == -1) {
		return NULL;
	}

	file = fopen(path, "r");
	if (!file) {
		addMessage(1, "Could not open %s", path);
		return NULL;
	}

	switch (cmd) {
	case mpc_dnp:
		activity(1, "Loading DNP list");
		break;
	case mpc_fav:
		activity(1, "Loading FAV list");
		break;
	default:
		activity(1, "Loading doublets");
		break;
	}

	buff = (char *) falloc(MAXPATHLEN + 4, 1);

	while (!feof(file)) {
		memset(buff, 0, MAXPATHLEN + 3);
		if (fgets(buff, MAXPATHLEN + 3, file) == NULL) {
			continue;
		}

		if (strlen(buff) > 1) {
			if (!bwlist) {
				bwlist = (marklist_t *) falloc(1, sizeof (marklist_t));
				ptr = bwlist;
			}
			else {
				ptr->next = (marklist_t *) falloc(1, sizeof (marklist_t));
				ptr = ptr->next;
			}

			if (!ptr) {
				addMessage(0, "Could not add %s", buff);
				goto cleanup;
			}

			strltcpy(ptr->dir, buff, strlen(buff));
			ptr->next = NULL;
			cnt++;
		}
	}

	addMessage(1, "Marklist %s with %i entries.", path, cnt);

  cleanup:
	free(buff);
	fclose(file);

	return bwlist;
}

int32_t writeList(const mpcmd_t cmd) {
	int32_t cnt = 0;
	marklist_t *runner;
	char path[MAXPATHLEN];
	FILE *fp = NULL;

	if (getListPath(cmd, path)) {
		/* No active profile */
		return -1;
	}

	if (MPC_CMD(cmd) == mpc_fav) {
		runner = getConfig()->favlist;
	}
	else {
		runner = getConfig()->dnplist;
	}

	fileBackup(path);

	fp = fopen(path, "w");

	if (NULL == fp) {
		addMessage(0, "Could not open %s for writing ", path);
		return -1;
	}

	while (runner != NULL) {
		if (fprintf(fp, "%s\n", runner->dir) == -1) {
			addMessage(0, "Could write %s to %s!", runner->dir, path);
			fclose(fp);
			return -1;
		};
		runner = runner->next;
		cnt++;
	}
	fclose(fp);

	return cnt;
}

/**
 * removes an entry from the favourite or DNP list
 */
int32_t delFromList(const mpcmd_t cmd, const char *line) {
	marklist_t *list;
	marklist_t *buff = NULL;
	mpcmd_t mode = MPC_CMD(cmd);
	int32_t cnt = 0;

	if (mode == mpc_deldnp) {
		mode = mpc_dnp;
	}
	if (mode == mpc_delfav) {
		mode = mpc_fav;
	}

	if (mode == mpc_dnp) {
		list = getConfig()->dnplist;
	}
	else if (mode == mpc_fav) {
		list = getConfig()->favlist;
	}
	else {
		addMessage(0, "Illegal listselection %s!", mpcString(cmd));
		return -1;
	}

	if (list == NULL) {
		addMessage(0, "No list to remove %s from!", line);
		return -1;
	}

	/* check the root entry first */
	while ((list != NULL) && (strcmp(list->dir, line) == 0)) {
		buff = list->next;
		free(list);
		list = buff;
		cnt++;
	}

	/* did we remove the first entry? */
	if (cnt > 0) {
		if (mode == mpc_dnp) {
			getConfig()->dnplist = list;
		}
		else {
			getConfig()->favlist = list;
		}
	}

	/* check on */
	while ((list != NULL) && (list->next != NULL)) {
		if (strcmp(list->next->dir, line) == 0) {
			buff = list->next;
			list->next = buff->next;
			free(buff);
			cnt++;
		}
		else {
			list = list->next;
		}
	}

	addMessage(1, "Removed %i entries", cnt);

	if (cnt > 0) {
		writeList(mode);
		applyLists(1);
	}

	return cnt;
}

/**
 * moves an entry in the playlist
 */
static void movePLEntry(mpplaylist_t * entry, mpplaylist_t * pos) {

	if ((entry == NULL) || (pos == NULL) ||
		(pos == entry) || (pos->prev == entry)) {
		return;
	}

	/* remove entry from old position */
	if (entry->prev != NULL) {
		entry->prev->next = entry->next;
	}
	if (entry->next != NULL) {
		entry->next->prev = entry->prev;
	}

	/* Insert entry into new position */
	entry->next = pos;
	entry->prev = pos->prev;
	if (pos->prev != NULL) {
		pos->prev->next = entry;
	}
	pos->prev = entry;

	/* we tinkered the playlist, an update would be nice */
	notifyChange(MPCOMM_TITLES);
}

/*
 * Searches for the given title in the playlist. Returns
 * the playlist entry on success and NULL on failure.
 * The search will ignore the current title!
 *
 * range 0 - return null
 * range 1 - only return titles to be played
 * range 2 - only return titles that have been played
 * range 3 - return any title
 */
static mpplaylist_t *getPLEntry(mptitle_t * title, int32_t range) {
	mpplaylist_t *runner = getCurrent();

	assert(runner != NULL);

	if (range & 2) {
		while (runner->prev != NULL) {
			runner = runner->prev;
		}
	}

	while (runner != NULL) {
		if (runner != getCurrent()) {
			if (runner->title == title) {
				return runner;
			}
		}
		else if (!(range & 1)) {
			return NULL;
		}
		runner = runner->next;
	}

	return NULL;
}

/*
 * moves a title in the playlist.
 * if after is NULL the current title is chosen as title to
 * insert after.
 */
static void moveTitle(mptitle_t * from, mptitle_t * before) {
	mpplaylist_t *frompos = NULL;
	mpplaylist_t *topos = NULL;

	if ((from == NULL) || (from == before)) {
		return;
	}

	/* check played titles */
	frompos = getPLEntry(from, 2);
	if (frompos == NULL) {
		/* check following titles */
		frompos = getPLEntry(from, 1);
	}

	if (before == NULL) {
		topos = getCurrent();
	}
	else {
		topos = getPLEntry(before, 3);
	}

	if (topos == NULL) {
		addMessage(0, "Nowhere to insert");
		return;
	}

	if (frompos == NULL) {
		/* add title as new one - should not happen */
		addMessage(1, "Inserting %s as new!", from->display);
		addToPL(from, topos, 0);
	}
	else {
		movePLEntry(frompos, topos);
	}
}

/*
 * moves a title within the playlist.
 * title and target are given as indices, like they would return
 * from the client.
 */
void moveTitleByIndex(uint32_t from, uint32_t before) {
	mptitle_t *frompos = NULL;
	mptitle_t *topos = NULL;

	if ((from == 0) || (from == before)) {
		return;
	}

	frompos = getTitleByIndex(from);
	if (frompos == NULL) {
		addMessage(0, "No title with index %u", from);
		return;
	}

	if (before != 0) {
		topos = getTitleByIndex(before);
		if (topos == NULL) {
			addMessage(0, "No target with index %u", from);
			return;
		}
	}

	moveTitle(frompos, topos);
}

/**
 * load a standard m3u playlist into a list of titles that the tools can handle
 */
mptitle_t *loadPlaylist(const char *path) {
	FILE *fp;
	int32_t cnt = 0;
	mptitle_t *current = NULL;
	char *buff;
	char titlePath[MAXPATHLEN];
	char mdir[MAXPATHLEN + 1] = "";
	int32_t i = 0;

	/* get path to the playlist, if any */
	if (NULL != strrchr(path, '/')) {
		i = MIN(strlen(path), MAXPATHLEN);

		while (path[i] != '/') {
			i--;
		}
		strtcpy(mdir, path, i + 1);
	}


	fp = fopen(path, "r");
	if (!fp) {
		/* try standard playlist directory */
		/* getenv("HOME") was valid before already.. */
		snprintf(titlePath, MAXPATHLEN - 1, "%s/.mixplay/playlists/%s",
				 getenv("HOME"), path);
		fp = fopen(titlePath, "r");
		if (!fp) {
			addMessage(0, "Could not open playlist %s", path);
			return NULL;
		}
		else {
			addMessage(1, "Using %s", titlePath);
			strtcpy(mdir, getConfig()->musicdir, MAXPATHLEN);
		}
	}

	buff = (char *) falloc(MAXPATHLEN, 1);
	activity(0, "Loading playlist");
	while (!feof(fp)) {
		if ((fgets(buff, MAXPATHLEN, fp) != NULL) &&
			(strlen(buff) > 1) && (buff[0] != '#')) {
			/* turn relative paths into absolute ones */
			if (buff[0] != '/') {
				strtcpy(titlePath, mdir, MAXPATHLEN - 1);
				strtcat(titlePath, buff, MAXPATHLEN - 1);
				strtcpy(buff, titlePath, MAXPATHLEN - 1);
			}
			/* remove control chars like CR/LF */
			strip(titlePath, buff, MAXPATHLEN - 1);
			current = insertTitle(current, titlePath);
			cnt++;
			/* turn list into playlist too */
		}
	}
	free(buff);

	fclose(fp);

	addMessage(2, "Playlist %s with %i entries.", path, cnt);

	return (current == NULL) ? NULL : current->next;
}

/**
 * Insert an entry into the database list and fill it with
 * path and if available, mp3 tag info.
 */
mptitle_t *insertTitle(mptitle_t * base, const char *path) {
	mptitle_t *root;

	root = (mptitle_t *) falloc(1, sizeof (mptitle_t));

	if (NULL == base) {
		base = root;
		base->next = base;
		base->prev = base;
	}
	else {
		root->next = base->next;
		root->prev = base;
		base->next = root;
		root->next->prev = root;
	}

	strtcpy(root->path, path, MAXPATHLEN);
	fillTagInfo(root);

	return root;
}

/**
 * return the number of titles in the list
 *
 * inc: MP bitmap of flags to include, MP_ALL for all
 * exc: MP bitmap of flags to exclude, 0 for all
 *
 * MP_DNP|MP_FAV will match any title where either flag is set
 */
uint64_t countTitles(const uint32_t inc, const uint32_t exc) {
	uint64_t cnt = 0;
	mptitle_t *base = getConfig()->root;
	mptitle_t *runner = base;

	if (NULL == base) {
		addMessage(1, "Counting without Database!");
		return 0;
	}

	do {
		if (((inc == MP_ALL) || (runner->flags & inc)) &&
			!(runner->flags & exc)) {
			cnt++;
		}

		runner = runner->prev;
	}
	while (runner != base);

	return cnt;
}

/**
 * returns the lowest playcount of the current list
 */
uint32_t getPlaycount(bool high) {
	mptitle_t *base = getConfig()->root;
	mptitle_t *runner = base;
	uint32_t min = UINT_MAX;
	uint32_t max = 0;
	uint32_t playcount;

	if (base == NULL) {
		addMessage(0, "Trying to get lowest playcount from empty database!");
		return 0;
	}

	do {
		if (getFavplay()) {
			playcount = runner->favpcount;
		}
		else {
			playcount = runner->playcount;
		}
		if (!(runner->flags & (MP_HIDE))) {
			if (playcount < min) {
				min = playcount;
			}
		}
		if (playcount > max) {
			max = playcount;
		}
		runner = runner->next;
	}
	while (runner != base);

	return high ? max : min;
}

static mptitle_t *skipOverFlags(mptitle_t * current, int32_t dir,
								uint32_t flags) {
	mptitle_t *marker = current;

	if (marker == NULL) {
		return NULL;
	}

	while ((marker->flags & (MP_DBL | MP_DNP)) ||
		   (marker->flags & flags) ||
		   (getFavplay() && !(marker->flags & MP_FAV))) {
		if (dir > 0) {
			marker = marker->next;
		}
		else {
			marker = marker->prev;
		}

		if (marker == current) {
			addMessage(3, "Ran out of titles!");
			return NULL;
		}
	}

	return marker;
}

/**
 * skips the global list until a title is found that has not been hidden
 * is not in the current playlist and is not marked as DNP/DBL
 * returns NULL if no title is available
 */
static mptitle_t *skipOver(mptitle_t * current, int32_t dir) {
	return skipOverFlags(current, dir, MP_INPL | MP_TDARK | MP_PDARK);
}

static char flagToChar(int32_t flag) {
	if (flag & MP_DNP) {
		if (flag & MP_DBL)
			return '2';
		else
			return 'D';
	}
	else if (flag & MP_FAV) {
		return 'F';
	}
	else if (flag & MP_INPL) {
		return '+';
	}
	else {
		return '-';
	}
}

/**
 * skips steps titles that match playcount pcount.
 */
static mptitle_t *skipPcount(mptitle_t * guard, int32_t steps,
							 uint32_t * pcount, uint64_t maxcount) {
	mptitle_t *runner = guard;

	/* zero steps is a bad idea but may happen, since we play with randum
	 * numbers */
	if (steps == 0) {
		steps = 1;
	}

	while (steps != 0) {
		/* fetch the next */
		if (steps > 0) {
			runner = skipOver(runner->next, 1);
		}
		else {
			runner = skipOver(runner->prev, -1);
		}
		/* Nothing fits!? Then increase playcount and try again */
		if (runner == NULL) {
			(*pcount)++;
			/* remove MP_PDARK as the playcount changed */
			unsetFlags(MP_PDARK);
			addMessage(2, "Increasing maxplaycount to %" PRIi32 " (pcount)",
					   *pcount);
			if (*pcount > maxcount) {
				/* We may need to decrease repeats */
				addMessage(3, "No more titles available");
				return NULL;
			}
			runner = skipOver(guard, steps);
			if (runner == NULL) {
				addMessage(2, "All titles are marked!");
				return NULL;
			}
		}

		/* Does it fit the playcount? */
		if ((!getFavplay() && (runner->playcount <= *pcount)) ||
			((runner->flags & MP_FAV) && (runner->favpcount <= *pcount))) {
			if (steps > 0)
				steps--;
			if (steps < 0)
				steps++;
		}
		else {
			runner->flags |= MP_PDARK;
		}
	}

	return runner;
}

/**
 * checks how many different artists are available to get a maximum for
 * how many titles can be played until an artist must be played again.
 * The actual value may vary during play, but it may never get larger
 * than this one, so avoid trying past this one.
 * The absolute maximum is 20, as we can only have 21 titles in the playlist
 * and checking further does not work.
 * The value is set in the global config.
 */
void setArtistSpread() {
	mptitle_t *runner = skipOver(getConfig()->root, 1);
	mptitle_t *checker = NULL;
	uint32_t count = 0;

	/* which titles should be skipped
	 * MP_PDARK is kind of questionable but may speed up adding titles and
	 * avoid premature increase of playcount */
	const uint32_t mask = MP_PDARK | MP_MARK;

	/* Use MP_MARK to check off tested titles */
	unsetFlags(MP_MARK);
	activity(1, "Checking artist spread");
	while (runner != NULL) {
		/* find the next unmarked, playable title to compare to */
		checker = skipOverFlags(runner->next, 1, mask);
		/* a comparison can be done */
		while (checker && (checker != runner)) {
			if (checkSim(runner->artist, checker->artist)) {
				/* the artist is similar enough, mark as gone */
				checker->flags |= MP_MARK;
			}
			/* check for the next title */
			checker = skipOverFlags(checker->next, 1, mask);
		}
		/* Check has been done
		 * 30 is correct here since we use a rule of 2/3 later */
		if (count++ == 30) {
			break;
		}
		/* runner has been checked too */
		runner->flags |= MP_MARK;
		/* find the next title to check */
		runner = skipOverFlags(runner->next, 1, mask);
	}
	/* clean up */
	unsetFlags(MP_MARK);

	/* two thirds to take number of titles per artist somewhat into account */
	count = (count * 2) / 3;
	getConfig()->spread = (count > 1) ? count : 2;
	addMessage(2, "At least %" PRIu32 " artists available.", count);
}

/**
 * adds a new title to the current playlist
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in the list
 * - prefers titles with lower playcount
 *
 * returns the head/current of the (new/current) playlist or NULL on error
 */
static int32_t addNewTitle(void) {
	mptitle_t *runner = NULL;
	mptitle_t *guard = NULL;
	uint64_t num = 0;
	char *lastpat = NULL;
	uint32_t pcount = 0;
	uint32_t maxpcount = 0;
	uint32_t tnum = 0;
	uint32_t maxnum = 0;

	mpplaylist_t *pl = getCurrent();
	mptitle_t *root;

	if (pl == NULL) {
		root = getConfig()->root;
	}
	else {
		while (pl->next != NULL) {
			pl = pl->next;
		}
		root = pl->title;
		/* just to set lastpat != NULL */
		lastpat = runner->artist;
	}
	runner = root;

	/* remember playcount bounds */
	pcount = getPlaycount(false);
	maxpcount = getPlaycount(true);
	addMessage(2, "Playcount [%" PRIu32 ":%" PRIu32 "]", pcount, maxpcount);

	/* are there playable titles at all? */
	if (countTitles(getFavplay()? MP_FAV : MP_ALL, MP_HIDE) == 0) {
		fail(F_FAIL, "No titles to be played!");
		return 0;
	}

	num = countTitles(getFavplay()? MP_FAV : MP_ALL, MP_HIDE | MP_PDARK);
	while (num < 3) {
		pcount++;
		/* if this happens, something is really askew */
		assert(pcount <= maxpcount);
		addMessage(2, "Less than 3 titles, bumping playcount to %" PRIu32,
				   pcount);
		unsetFlags(MP_PDARK);
		num = countTitles(getFavplay()? MP_FAV : MP_ALL, MP_HIDE);
	}
	maxnum = getConfig()->spread;
	addMessage(2, "%" PRIu64 " titles available, avoiding %u repeats", num,
			   maxnum);

	/* start with some 'random' title */
	runner =
		skipPcount(runner, (int32_t) ((num / 2) - (random() % num)), &pcount,
				   maxpcount);
	if (runner == NULL) {
		addMessage(1, "Off to a bad start!");
		runner = root;
	}

	if (lastpat == NULL) {
		/* No titles in the playlist yet, we're done! */
		getConfig()->current = appendToPL(runner, NULL, 1);
		return 1;
	}

	/* step through the playlist and check for repeats */
	do {
		/* skip searched titles */
		if (pl->title->flags & MP_INPL) {
			lastpat = pl->title->artist;
			guard = runner;
			/* does the title clash with the current one? */
			while (checkSim(runner->artist, lastpat)) {
				addMessage(3, "%s = %s", runner->artist, lastpat);
				/* don't try this one again */
				runner->flags |= MP_TDARK;
				/* get another with a matching playcount
				 * these are expensive, so we try to keep the steps
				 * somewhat reasonable.. */
				runner =
					skipPcount(runner, (num / 2) - (random() % num),
							   &pcount, maxpcount);
				if (runner == NULL) {
					/* back to square one for this round */
					runner = guard;
					if (maxnum > 1) {
						maxnum--;
						pcount = getPlaycount(false);
						unsetFlags(MP_TDARK | MP_PDARK);
						num =
							countTitles(getFavplay()? MP_FAV : MP_ALL,
										MP_HIDE);
						/* we do not want to see this and if we do, it may hint at
						 * something strange going on - maybe even add some debug info */
						addMessage(0,
								   "Reducing repeat to %" PRIu32 " with %"
								   PRIu64 " titles", maxnum, num);
					}
					else {
						/* This should rather change it to something sensible instead of
						 * simply bailing out! */
						fail(-1, "Cannot play this profile!");
					}
				}
			}

			if (guard != runner) {
				/* title did not fit, start again from the beginning
				 * with the new one that fits here */
				while (pl->next != NULL) {
					pl = pl->next;
				}
				tnum = 0;
				continue;
			}
			tnum++;
		}
		pl = pl->prev;
	} while ((pl != NULL) && (tnum < maxnum));

	/*  *INDENT-OFF*  */
	addMessage(2, "[+] (%i/%i/%c) %5" PRIu32 " %s",
			   (runner->flags & MP_FAV) ? runner->favpcount : runner->playcount,
			   pcount, flagToChar(runner->flags), runner->key, runner->display);
	/*  *INDENT-ON*  */
	appendToPL(runner, getCurrent(), 1);
	return 1;
}

/**
 * checks the current playlist.
 * If there are more than 10 previous titles, those get pruned. Titles marked
 * as DNP or Doublet will be removed as well.
 *
 * If fill is set, the playlist will be filled up to ten new titles.
 */
void plCheck(bool fill) {
	int32_t cnt = 0;
	mpplaylist_t *pl;
	mpplaylist_t *buf;

	/* make sure the playlist is not modifid elsewhere right now */
	lockPlaylist();

	pl = getCurrent();
	/* there is a playlist, so clean up */
	if (pl != NULL) {
		/* It's a stream, so truncate stream title history to 20 titles */
		if (getConfig()->mpmode & PM_STREAM) {
			while ((pl->next != NULL) && (cnt < 20)) {
				pl = pl->next;
				cnt++;
			}

			buf = pl->next;
			pl->next = NULL;
			while (buf != NULL) {
				pl = buf->next;
				free(buf->title);
				free(buf);
				buf = pl;
			}
			notifyChange(MPCOMM_TITLES);
			unlockPlaylist();
			return;
		}

		/* No stream but standard mixplaylist */
		cnt = 0;

		/* rewind to the start of the list */
		while (pl->prev != NULL) {
			pl = pl->prev;
		}

		/* go through end of the playlist and clean up underway */
		while (pl->next != NULL) {
			/* clean up on the way to remove DNP marked or deleted files that
			 * have not been added through a search.
			 * There /should/ not be any doublets here, but it does not hurt
			 * to check those too */
			if (((pl->title->flags & MP_INPL)
				 && (pl->title->flags & (MP_DNP | MP_DBL)))
				|| (!mp3Exists(pl->title))) {
				/* make sure that the playlist root stays valid */
				if (pl == getCurrent()) {
					if (pl->prev != NULL) {
						getConfig()->current = pl->prev;
					}
					else {
						getConfig()->current = pl->next;
					}
				}
				pl = remFromPL(pl);
			}
			else {
				pl = pl->next;
			}
		}

		/* Done cleaning, now start pruning */
		/* truncate playlist title history to 10 titles */
		cnt = 0;
		pl = getCurrent();
		while ((pl->prev != NULL) && (cnt < 10)) {
			pl = pl->prev;
			cnt++;
		}

		/* cut off playlist */
		buf = pl->prev;
		pl->prev = NULL;

		/* clean up loose ends */
		while (buf != NULL) {
			pl = buf->prev;
			remFromPL(buf);
			buf = pl;
		}

		/* Count titles to come */
		cnt = 0;
		pl = getCurrent();
		while (pl->next != NULL) {
			pl = pl->next;
			cnt++;
		}
	}

	/* fill up the playlist with new titles */
	if (fill) {
		unsetFlags(MP_TDARK);
		while (cnt < 10) {
			activity(0, "Add title %i", cnt);
			addNewTitle();
			cnt++;
		}
	}

	unlockPlaylist();
	notifyChange(MPCOMM_TITLES);
}

/*
 * Steps recursively through a directory and collects all music files in a list
 * curdir: current directory path
 * files:  the list to store filenames in
 * returns the LAST entry of the list.
 */
mptitle_t *recurse(char *curdir, mptitle_t * files) {
	char dirbuff[2 * MAXPATHLEN];
	struct dirent **entry;
	int32_t num, i;

	/* this means the config is broken */
	assert(curdir != NULL);

	if ('/' == curdir[strlen(curdir) - 1]) {
		curdir[strlen(curdir) - 1] = 0;
	}

	addMessage(3, "Checking %s", curdir);

	/* get all music files */
	num = getMusic(curdir, &entry);

	if (num < 0) {
		addMessage(0, "getMusic failed in %s", curdir);
		return files;
	}

	for (i = 0; i < num; i++) {
		activity(0, "Scanning");
		sprintf(dirbuff, "%s/%s", curdir, entry[i]->d_name);
		files = insertTitle(files, dirbuff);
		free(entry[i]);
	}

	free(entry);

	/* step down subdirectories */
	num = getDirs(curdir, &entry);

	if (num < 0) {
		addMessage(0, "getDirs failed on %s", curdir);
		return files;
	}

	for (i = 0; i < num; i++) {
		sprintf(dirbuff, "%s/%s", curdir, entry[i]->d_name);
		files = recurse(dirbuff, files);
		free(entry[i]);
	}

	free(entry);

	return files;
}

/**
 * does a database scan and dumps information about playrate
 * favourites and DNPs
 */
void dumpInfo(int32_t smooth) {
	mptitle_t *root = getConfig()->root;
	mptitle_t *current = root;
	uint32_t maxplayed = 0;
	uint32_t pl = 0;
	uint32_t dnp = 0;
	uint32_t dbl = 0;
	uint32_t fav = 0;
	uint32_t marked = 0;
	uint32_t numtitles = 0;
	uint32_t fixed = 0;

	do {
		if (current->flags & MP_FAV) {
			fav++;
		}
		if (current->flags & MP_DNP) {
			dnp++;
		}
		if (current->flags & MP_DBL) {
			dbl++;
		}
		if (current->flags & MP_INPL) {
			marked++;
		}
		if (getFavplay()) {
			if (current->favpcount > maxplayed) {
				maxplayed = current->favpcount;
			}
		}
		else {
			if (current->playcount > maxplayed) {
				maxplayed = current->playcount;
			}
		}
		numtitles++;
		current = current->next;
	} while (current != root);

	addMessage(0, "%5i titles in Database", numtitles);
	if (fav)
		addMessage(0, "%5i favourites", fav);
	if (dnp) {
		addMessage(0, "%5i do not plays", dnp);
		if (dbl)
			addMessage(0, "%5i doublets", dbl);
	}
	if (marked)
		addMessage(0, "%5i in playlist", marked);
	addMessage(0, "-- Playcount --");

	while (pl <= maxplayed) {
		uint32_t pcount = 0;
		uint32_t dcount = 0;
		uint32_t dblcnt = 0;
		uint32_t favcnt = 0;
		uint32_t markcnt = 0;
		char line[MAXPATHLEN];

		do {
			if ((getFavplay() && current->favpcount == pl) ||
				(!getFavplay() && current->playcount == pl)) {
				pcount++;
				if (current->flags & MP_DNP) {
					dcount++;
				}
				if (current->flags & MP_DBL) {
					dblcnt++;
				}
				if (current->flags & MP_FAV) {
					favcnt++;
				}
				if (current->flags & MP_INPL) {
					markcnt++;
				}
			}
			current = current->next;
		} while (current != root);

		/* just a few titles (< 0.5%) with playcount == pl ? Try to close the gap */
		if (smooth && !getFavplay() && (pcount < numtitles / 200)) {
			fixed = 1;
			do {
				if (current->playcount > pl) {
					current->playcount--;
					if (current->favpcount > 0) {
						current->favpcount--;
					}
				}
				current = current->next;
			} while (current != root);
			maxplayed--;
		}

		if (!isDebug())
			pcount = pcount - (dcount + dblcnt);

		/* normal output and forward to next count */
		if (pcount > 0) {
			switch (pl) {
			case 0:
				sprintf(line, "    Never %5i", pcount);
				break;
			case 1:
				sprintf(line, "     Once %5i", pcount);
				break;
			case 2:
				sprintf(line, "    Twice %5i", pcount);
				break;
			default:
				sprintf(line, "%3i times %5i", pl, pcount);
			}
#define DD (isDebug()?'/':'+')
			if (favcnt || dcount)
				if (favcnt == pcount)
					addMessage(0, "%s - allfav", line);
				else if (dcount == pcount)
					addMessage(0, "%s - alldnp", line);
				else if (favcnt == 0)
					if (dblcnt == 0)
						addMessage(0, "%s %c %i dnp", line, DD, dcount);
					else
						addMessage(0, "%s %c %i dbl %c %i dnp", line, DD,
								   dcount, DD, dblcnt);
				else if (dcount == 0)
					addMessage(0, "%s / %i fav", line, favcnt);
				else if (dblcnt == 0)
					addMessage(0, "%s %c %i dnp / %i fav", line, DD, dcount,
							   favcnt);
				else
					addMessage(0, "%s %c %i dbl %c %i dnp / %i fav", line, DD,
							   dcount, DD, dblcnt, favcnt);
			else
				addMessage(0, "%s", line);
#undef DD
		}
		pl++;
	}							/* while pl < maxplay */

	if (fixed) {
		dbWrite(1);
	}
}

/**
 * big debug dump, used on ctrl-c or whenever something is fishy
 */
void dumpState() {
	mpconfig_t *conf = getConfig();
	mptitle_t *guard = conf->root;

	addMessage(0, "Status: %s", mpcString(conf->status));
	addMessage(0, "database: %p - mode: 0x%02x", (void *) guard, conf->mpmode);
	if ((guard != NULL) && (conf->mpmode & PM_DATABASE)) {
		addMessage(0, "%5" PRIu64 " titles are MP_INPL", countflag(MP_INPL));
		addMessage(0, "%5" PRIu64 " titles are MP_PDARK", countflag(MP_PDARK));
		addMessage(0, "%5" PRIu64 " titles are MP_TDARK", countflag(MP_TDARK));
		addMessage(0, "%5" PRIu64 " titles are MP_HIDE", countflag(MP_HIDE));
		dumpInfo(0);
	}
	else {
		addMessage(0, "No title database");
	}
}

static int32_t addRangePrefix(mpcmd_t cmd, char *line) {
	line[2] = 0;
	line[1] = MPC_ISFUZZY(cmd) ? '*' : '=';
	switch (MPC_RANGE(cmd)) {
	case mpc_title:
		line[0] = 't';
		break;
	case mpc_artist:
		line[0] = 'a';
		break;
	case mpc_album:
		line[0] = 'l';
		break;
	case mpc_genre:
		line[0] = 'g';
		break;
	case mpc_display:
		line[0] = 'd';
		break;
	default:
		addMessage(1, "Unknown range %02x", MPC_RANGE(cmd) >> 8);
		return -1;
	}

	return 0;
}

static int32_t rangeToLine(mpcmd_t cmd, const mptitle_t * title,
						   char line[MAXPATHLEN + 2]) {
	if (addRangePrefix(cmd, line) == 0) {
		switch (MPC_RANGE(cmd)) {
		case mpc_title:
			strltcat(line, title->title, NAMELEN + 2);
			break;
		case mpc_artist:
			strltcat(line, title->artist, NAMELEN + 2);
			break;
		case mpc_album:
			strltcat(line, title->album, NAMELEN + 2);
			break;
		case mpc_genre:
			strltcat(line, title->genre, NAMELEN + 2);
			break;
		case mpc_display:
			strltcat(line, title->display, MAXPATHLEN + 2);
			break;
		default:
			addMessage(0, "Illegal range 0x%04x", MPC_RANGE(cmd));
			break;
		}
		return 0;
	}
	return 1;
}

/**
 * wrapper to handle FAV and DNP. Uses the given title and the range
 * definitions in cmd to create the proper config line and immediately
 * applies it to the current database and playlist
 */
int32_t handleRangeCmd(mpcmd_t cmd, mptitle_t * title) {
	char line[MAXPATHLEN + 2];
	marklist_t *buff, *list;
	int32_t cnt = -1;
	mpconfig_t *config = getConfig();

	if (rangeToLine(cmd, title, line) == 0) {
		if (strlen(line) < 5) {
			addMessage(0, "Not enough info in %s!", line);
			return 0;
		}

		if (MPC_CMD(cmd) == mpc_fav) {
			list = config->favlist;
		}
		else {
			list = config->dnplist;
		}

		/* check if the entry is already in the list */
		buff = list;
		while (buff != NULL) {
			if (strcmp(line, buff->dir) == 0) {
				addMessage(0, "%s already in list!", line);
				return -1;
			}
			buff = buff->next;
		}

		/* create new entry item */
		buff = (marklist_t *) falloc(1, sizeof (marklist_t));
		strcpy(buff->dir, line);
		buff->next = NULL;

		/* if there is no existing list yet, the new item becomes the list */
		if (list == NULL) {
			if (MPC_CMD(cmd) == mpc_fav) {
				config->favlist = buff;
			}
			else {
				config->dnplist = buff;
			}
		}
		/* otherwise append the ittem to the list */
		else {
			while (list->next != NULL) {
				list = list->next;
			}
			list->next = buff;
		}

		/* add line to the file */
		addToList(buff->dir, cmd);

		/* apply actual line to the playlist */
		if (MPC_CMD(cmd) == mpc_fav) {
			cnt = applyFAVlist(buff);
		}
		else if (MPC_CMD(cmd) == mpc_dnp) {
			cnt = applyDNPlist(buff, 0);
		}
	}

	return cnt;
}

int32_t delTitleFromOtherList(mpcmd_t cmd, const mptitle_t * title) {
	char *line = NULL;
	int32_t rv = 0;

	if ((MPC_CMD(cmd) != mpc_fav) && (MPC_CMD(cmd) != mpc_dnp)) {
		addMessage(0, "No other list for %s!", mpcString(cmd));
	}

	line = (char *) falloc(MAXPATHLEN + 2, 1);
	if (rangeToLine(cmd, title, line) == 0) {
		rv = delFromList(cmd == mpc_fav ? mpc_dnp : mpc_fav, line);
	}
	free(line);
	return rv;
}


/**
 * Adds a title to the global doublet list
 */
int32_t handleDBL(mptitle_t * title) {
	char line[MAXPATHLEN + 2] = "p=";
	marklist_t *buff;
	mpconfig_t *config = getConfig();
	marklist_t *list = config->dbllist;

	strltcat(line, title->path, MAXPATHLEN + 1);

	buff = list;
	while (buff != NULL) {
		if (strcmp(line, buff->dir) == 0) {
			addMessage(1, "%s already in list!", line);
			return -1;
		}
		buff = buff->next;
	}

	buff = (marklist_t *) falloc(1, sizeof (marklist_t));
	strcpy(buff->dir, line);
	buff->next = NULL;

	if (list == NULL) {
		config->dbllist = buff;
	}
	else {
		while (list->next != NULL) {
			list = list->next;
		}
		list->next = buff;
	}

	addToList(buff->dir, mpc_doublets);
	return applyDNPlist(buff, 1);
}
