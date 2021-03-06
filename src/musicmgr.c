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

/*
 * checks if pat has a matching in text. If text is shorter than pat then
 * the test fails.
 */
static int patMatch(const char *text, const char *pat) {
	char *lotext;
	char *lopat;
	int best = 0;
	int res;
	int plen = strlen(pat);
	int tlen = strlen(text);
	int i, j;
	int guard;

	/* The pattern must not be longer than the text! */
	if (tlen < plen) {
		return 0;
	}

	/* prepare the pattern */
	lopat = (char *) falloc(strlen(pat) + 3, 1);
	strltcpy(lopat + 1, pat, plen + 1);
	lopat[0] = -1;
	lopat[plen + 1] = -1;

	/* prepare the text */
	lotext = (char *) falloc(tlen + 1, 1);
	strltcpy(lotext, text, tlen + 1);

	/* check */
	for (i = 0; i <= (tlen - plen); i++) {
		res = 0;
		for (j = i; j < plen + i; j++) {
			if ((lotext[j] == lopat[j - i]) ||
				(lotext[j] == lopat[j - i + 1]) ||
				(lotext[j] == lopat[j - i + 2])) {
				res++;
			}
			if (res > best) {
				best = res;
			}
		}
	}

	/* tweak matchlevel to get along with special cases */
	guard = 75 + (20 * plen) / tlen;

	if (guard > 100)
		guard = 100;
	/* compute percentual match */
	res = (100 * best) / plen;
	if (res >= guard) {
		addMessage(4, "%s - %s - %i/%i", lotext, lopat, res, guard);
	}
	free(lotext);
	free(lopat);
	return (res >= guard);
}

/*
 * symmetric patMatch that checks if text is shorter than pattern
 * used for artist checking in addNewTitle
 */
static int checkSim(const char *text, const char *pat) {
	if (strlen(text) < strlen(pat)) {
		return patMatch(pat, text);
	}
	else {
		return patMatch(text, pat);
	}
}

int getListPath(char path[MAXPATHLEN], mpcmd_t cmd) {
	if (getConfig()->active < 1) {
		return -1;
	}
	if (MPC_CMD(cmd) == mpc_doublets) {
		snprintf(path, MAXPATHLEN, "%s/.mixplay/mixplay.dbl", getenv("HOME"));
	}
	else {
		snprintf(path, MAXPATHLEN, "%s/.mixplay/%s.", getenv("HOME"),
				 getConfig()->profile[getConfig()->active - 1]->name);
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
static int addToList(const char *line, mpcmd_t cmd) {
	FILE *fp;
	char path[MAXPATHLEN + 1];

	getListPath(path, cmd);

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
mpplaylist_t *appendToPL(mptitle_t * title, mpplaylist_t * pl, const int mark) {
	mpplaylist_t *runner = pl;

	if (runner != NULL) {
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
	pltitle->title->flags &= ~MP_MARK;

	free(pltitle);
	return ret;
}

/**
 * checks if the playcount needs to be increased and if the skipcount
 * needs to be decreased. In both cases the updated information is written
 * back into the db.
 * returns 1 if the title was marked DNP due to skipping
 */
int playCount(mptitle_t * title, int skip) {
	int rv = 0;

	/* playcount only makes sense with a title list */
	if (getConfig()->mpmode & PM_STREAM) {
		addMessage(1, "No skip on streams!");
		return -1;
	}

	/* not marked = searchplay, does not count
	 * just marked dnp? someone else may like it though */
	if (!(title->flags & MP_MARK) || (title->flags & (MP_DNP | MP_DBL))) {
		addMessage(2, "%s is dnp or not marked -> ignored", title->display);
		return -1;
	}

	/* skipcount only happens on database play */
	if (getConfig()->mpmode & PM_DATABASE) {
		if (skip) {
			title->skipcount++;
			if (title->skipcount >= getConfig()->skipdnp) {
				/* prepare resurrection */
				title->skipcount = 0;
				/* three strikes and you're out */
				addMessage(0, "Marked %s as DNP for skipping!",
						   title->display);
				handleRangeCmd(title, (mpcmd_t) (mpc_display | mpc_dnp));
				rv = 1;
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
		dbMarkDirty();
	}

	return rv;
}

/*
 * removes the title with the given key from the playlist
 * this returns NULL or a valid playlist anchor in case the current title was
 * removed
 */
mpplaylist_t *remFromPLByKey(mpplaylist_t * root, const unsigned key) {
	mpplaylist_t *pl = root;

	if (pl == NULL) {
		addMessage(0, "Cannot remove titles from an empty playlist");
		return NULL;
	}

	if (key == 0) {
		return root;
	}

	while (pl->prev != NULL) {
		pl = pl->prev;
	}

	while ((pl != NULL) && (pl->title->key != key)) {
		pl = pl->next;
	}

	if (pl != NULL) {
		/* This may be too hard */
		if (pl->title->flags & MP_MARK) {
			playCount(pl->title, 1);
		}
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

	return root;
}

/**
 * inserts a title into the playlist chain. Creates a new playlist
 * if no target is set.
 * This function returns a valid target
 * if mark is true, the title will be checked and marked for playing in
 * default mixplay, otherwise it is a searched title and will be
 * played out of order.
 */
mpplaylist_t *addToPL(mptitle_t * title, mpplaylist_t * target, const int mark) {
	mpplaylist_t *buf = NULL;

	if (mark && (title->flags & MP_MARK)) {
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
		title->flags |= MP_MARK;
	}
	/* do not notify here as the playlist may be a search result */
	return target;
}

/**
 * helperfunction for scandir() - just return unhidden directories and links
 */
static int dsel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			((entry->d_type == DT_DIR) || (entry->d_type == DT_LNK)));
}

/**
 * helperfunction for scandir() - just return unhidden regular music files
 */
static int msel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			(entry->d_type == DT_REG) && isMusic(entry->d_name));
}

/**
 * helperfunction for scandir() - just return unhidden regular playlist files
 */
static int plsel(const struct dirent *entry) {
	return ((entry->d_name[0] != '.') &&
			(entry->d_type == DT_REG) && endsWith(entry->d_name, ".m3u"));
}

/**
 * helperfunction for scandir() sorts entries numerically first then
 * alphabetical
 */
static int tsort(const struct dirent **d1, const struct dirent **d2) {
	int res = atoi((*d1)->d_name) - atoi((*d2)->d_name);

	if (res == 0) {
		res = strcasecmp((*d1)->d_name, (*d2)->d_name);
	}
	return res;
}

/**
 * loads all playlists in cd into pllist
 */
int getPlaylists(const char *cd, struct dirent ***pllist) {
	return scandir(cd, pllist, plsel, tsort);
}

/**
 * loads all music files in cd into musiclist
 */
static int getMusic(const char *cd, struct dirent ***musiclist) {
	return scandir(cd, musiclist, msel, tsort);
}

/**
 * loads all directories in cd into dirlist
 */
static int getDirs(const char *cd, struct dirent ***dirlist) {
	return scandir(cd, dirlist, dsel, alphasort);
}

/**
 * compare two strings and ignore the case.
 * returns 1 on equality and 0 on differerences.
 * This is not equal to stricmp/strcasecmp!
 */
static int strieq(const char *t1, const char *t2) {
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
 * range is only used to mark fuzzy searches!
 */
static int isMatch(const char *term, const char *pat, const mpcmd_t range) {
	char loterm[MAXPATHLEN];

	strltcpy(loterm, term, MAXPATHLEN);

	if (MPC_ISFUZZY(range)) {
		return patMatch(loterm, pat);
	}

	if (MPC_ISSUBSTR(range)) {
		return (strstr(loterm, pat) != NULL);
	}

	strltcpy(loterm, term, MAXPATHLEN);
	return (strcmp(loterm, pat) == 0);
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
static unsigned matchTitle(mptitle_t * title, const char *pat) {
	int fuzzy = 0;
	int res = 0;

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
		res = isMatch(title->display, pat, fuzzy);
	}

	return res;
}

/**
 * fills the global searchresult structure with the results of the given search.
 * Returns the number of found titles.
 * pat - pattern to search for
 * range - search range
 */
int search(const char *pat, const mpcmd_t range) {
	mptitle_t *root = getConfig()->root;
	mptitle_t *runner = root;
	searchresults_t *res = getConfig()->found;
	unsigned int i = 0;
	unsigned valid = 0;
	mpcmd_t found = mpc_play;
	char *lopat;

	/* enter while the last result has not been sent yet! */
	assert(res->state != mpsearch_done);
	assert(pat != NULL);

	/* free buffer playlist, the arrays will not get lost due to the realloc later */
	res->titles = wipePlaylist(res->titles, 0);
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
		activity(1, "searching for %s", lopat);
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
				/* check for new artist - checksim to group typos and collabs
				 * TDOD: check if this is really as beneficial as expected */
				for (i = 0; (i < res->anum)
					 && !checkSim(res->artists[i], runner->artist); i++);
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
	return ((res->tnum > MAXSEARCH) ? -1 : (int) res->tnum);
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
int applyDNPlist(marklist_t * list, int dbl) {
	mptitle_t *base = getConfig()->root;
	mptitle_t *pos = base;
	marklist_t *ptr = list;
	mpplaylist_t *pl = getConfig()->current;
	int cnt = 0;
	unsigned range = 0;

	if (NULL == list) {
		return 0;
	}

	if (dbl)
		activity(1, "Applying DBL list");
	else
		activity(1, "Applying DNP list");

	do {
		if (!(pos->flags & MP_DBL)) {
			ptr = list;

			while (ptr) {
				range = matchTitle(pos, ptr->dir);
				if (range > MPC_RANGE(pos->flags)) {
					addMessage(3, "[D] %s: %s", ptr->dir, pos->display);
					pos->flags = (range | MP_DNP);
					if (dbl)
						pos->flags |= MP_DBL;
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
			if (pl == getConfig()->current) {
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
static int applyFAVlist(marklist_t * favourites) {
	marklist_t *ptr = NULL;
	mptitle_t *root = getConfig()->root;
	mptitle_t *runner = root;
	int cnt = 0;
	unsigned range = 0;

	if (NULL == root) {
		addMessage(0, "No music loaded for FAVlist");
		return -1;
	}

	if (favourites == NULL) {
		return 0;
	}

	activity(1, "Applying FAV list");

	do {
		if (!(runner->flags & MP_DBL)) {
			ptr = favourites;

			while (ptr) {
				range = matchTitle(runner, ptr->dir);
				if (range > MPC_RANGE(runner->flags)) {
					if (!(runner->flags & MP_FAV)) {
						addMessage(3, "[F] %s: %s", ptr->dir, runner->display);
						/* Save MP_MARK */
						runner->flags =
							(runner->flags & MP_MARK) | MP_FAV | range;
						/* This is correct! Both counters get increased once every round */
						runner->favpcount = runner->playcount;
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

void applyLists(int clean) {
	mpconfig_t *control = getConfig();
	mptitle_t *title = control->root;

	lockPlaylist();
	if (clean) {
		do {
			title->flags &= ~(MPC_DFRANGE | MP_FAV | MP_DNP);
			title = title->next;
		} while (title != control->root);
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
	int cnt = 0;

	if (getListPath(path, cmd) == -1) {
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

int writeList(const mpcmd_t cmd) {
	int cnt = 0;
	marklist_t *runner;
	char path[MAXPATHLEN];
	FILE *fp = NULL;

	if (getListPath(path, cmd)) {
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
int delFromList(const mpcmd_t cmd, const char *line) {
	marklist_t *list;
	marklist_t *buff = NULL;
	mpcmd_t mode;
	int cnt = 0;

	if (MPC_CMD(cmd) == mpc_deldnp) {
		mode = mpc_dnp;
		list = getConfig()->dnplist;
	}
	else if (MPC_CMD(cmd) == mpc_delfav) {
		mode = mpc_fav;
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
static mpplaylist_t *getPLEntry(mptitle_t * title, int range) {
	mpplaylist_t *runner = getConfig()->current;

	assert(runner != NULL);

	if (range & 2) {
		while (runner->prev != NULL) {
			runner = runner->prev;
		}
	}

	while (runner != NULL) {
		if (runner != getConfig()->current) {
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
	if (frompos != NULL) {
		from->flags &= ~MP_MARK;
	}
	else {
		/* check following titles */
		frompos = getPLEntry(from, 1);
	}

	if (before == NULL) {
		topos = getConfig()->current;
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
void moveTitleByIndex(unsigned from, unsigned before) {
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
	int cnt = 0;
	mptitle_t *current = NULL;
	char *buff;
	char titlePath[MAXPATHLEN];
	char mdir[MAXPATHLEN + 1] = "";
	int i = 0;

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
	while (!feof(fp)) {
		activity(1, "Loading");
		if ((fgets(buff, MAXPATHLEN, fp) != NULL) &&
			(strlen(buff) > 1) && (buff[0] != '#')) {
			/* turn relative paths into absolute ones */
			if (buff[0] != '/') {
				strtcpy(titlePath, mdir, MAXPATHLEN);
				strtcat(titlePath, buff, MAXPATHLEN);
				strtcpy(buff, titlePath, MAXPATHLEN);
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
unsigned long countTitles(const unsigned int inc, const unsigned int exc) {
	unsigned long cnt = 0;
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
unsigned getPlaycount(int high) {
	mptitle_t *base = getConfig()->root;
	mptitle_t *runner = base;
	unsigned min = UINT_MAX;
	unsigned max = 0;

	if (base == NULL) {
		addMessage(0, "Trying to get lowest playcount from empty database!");
		return 0;
	}

	do {
		if (!(runner->flags & (MP_DNP | MP_DBL))) {
			if (runner->playcount < min) {
				min = runner->playcount;
			}
		}
		if (runner->playcount > max) {
			max = runner->playcount;
		}
		runner = runner->next;
	}
	while (runner != base);

	return high ? max : min;
}

/**
 * skips the global list until a title is found that has not been played
 * is not in the current playlist and is not marked as DNP
 */
static mptitle_t *skipOver(mptitle_t * current, int dir) {
	mptitle_t *marker = current;

	if (marker == NULL) {
		addMessage(0, "No current title to skip over!");
		return NULL;
	}

	while ((marker->flags & (MP_DBL | MP_DNP | MP_MARK)) ||
		   (getFavplay() && !(marker->flags & MP_FAV))) {
		if (dir > 0) {
			marker = marker->next;
		}
		else {
			marker = marker->prev;
		}

		if (marker == current) {
			addMessage(0, "Ran out of titles!");
			return NULL;
		}
	}

	return marker;
}

/**
 * skips the given number of titles
 */
static mptitle_t *skipTitles(mptitle_t * current, long num) {
	if (NULL == current) {
		return NULL;
	}

	for (; num > 0; num--) {
		current = skipOver(current->next, 1);
		if (current == NULL) {
			addMessage(-1, "Database title ring broken!");
			return NULL;
		}
	}

	for (; num < 0; num++) {
		current = skipOver(current->prev, -1);
		if (current == NULL) {
			addMessage(-1, "Database title ring broken!");
			return NULL;
		}
	}

	return current;
}

static char flagToChar(int flag) {
	if (flag & MP_DNP) {
		if (flag & MP_DBL)
			return '2';
		else
			return 'D';
	}
	else if (flag & MP_FAV) {
		return 'F';
	}
	else if (flag & MP_MARK) {
		return '+';
	}
	else {
		return '-';
	}
}

/**
 * adds a new title to the current playlist
 *
 * Core functionality of the mixplay architecture:
 * - does not play the same artist twice in a row
 * - prefers titles with lower playcount
 *
 * returns the head/current of the (new/current) playlist or NULL on error
 */
mpplaylist_t *addNewTitle(mpplaylist_t * pl, mptitle_t * root) {
	mptitle_t *runner = NULL;
	mptitle_t *guard = NULL;
	unsigned long num = 0;
	char *lastpat = NULL;
	unsigned int pcount = 0;
	unsigned int cycles = 0;
	int valid = 0;

	/* 0 - nothing checked
	 * 1 - namecheck okay
	 * 2 - playcount okay
	 * 3 - all okay
	 */

	if (pl != NULL) {
		while (pl->next != NULL) {
			pl = pl->next;
		}
		runner = pl->title;
		lastpat = pl->title->artist;
	}
	else {
		valid = 1;
		runner = root;
	}

	/* select a random title from the database */
	/* skip forward until a title is found that is neither DNP nor MARK */
	num =
		countTitles(getFavplay()? MP_FAV : MP_ALL, MP_DBL | MP_DNP | MP_MARK);
	if (num == 0) {
		addMessage(-1, "No titles to be played!");
		return NULL;
	}
	runner = skipTitles(runner, random() % num);

	if (runner == NULL) {
		addMessage(-1, "No titles in the database!?");
		return NULL;
	}

	pcount = getPlaycount(0);

	cycles = 0;
	while ((valid & 3) != 3) {
		if (lastpat == NULL)
			valid |= 1;
		/* title did not pass namecheck */
		if ((valid & 1) != 1) {
			guard = runner;

			while (checkSim(runner->artist, lastpat)) {
				addMessage(3, "%s = %s", runner->artist, lastpat);
				activity(1, "Nameskipping");
				/* If we put pcount check here, we could simplify everything.. */
				runner = skipOver(runner->next, 1);

				/* hopefully this will not happen */
				if ((runner == guard) || (runner == NULL)) {
					addMessage(0, "Only %s left..", lastpat);
					return (addNewTitle(pl, root));
				}
			}
			addMessage(3, "%s != %s", runner->artist, lastpat);

			if (guard != runner) {
				/* we skipped and need to check playcount */
				valid = 1;
			}
			else {
				/* we did not skip, so if playcount was fine it's still fine */
				valid |= 1;
			}
		}

		guard = runner;
		/* title did not pass playcountcheck */
		while ((valid & 2) != 2) {
			if (runner->flags & MP_FAV) {
				/* favplay just uses favpcount */
				if (getFavplay()) {
					if (runner->favpcount <= pcount) {
						valid |= 2;
					}
				}
				/* normal play on favourite uses both */
				else if (runner->playcount + runner->favpcount <= 2 * pcount) {
					valid |= 2;
				}
			}
			/* not a fav, standard rule */
			else if (runner->playcount <= pcount) {
				valid |= 2;
			}

			if ((valid & 2) != 2) {
				valid = 0;
				activity(1, "Playcountskipping");
				/* simply pick a new title at random to avoid edge cases */
				/* runner=skipTitles( runner, rand()%num ); */
				/* TODO check if this is better or worse.. */
				runner = skipTitles(runner, 1);

				if (runner == guard) {
					pcount++;	/* allow more replays */
					addMessage(1, "Increasing maxplaycount to %i (skip)",
							   pcount);
				}
			}
		}

		/* 10 may not be enough in practice */
		if (++cycles > 10) {
			cycles = 0;
			pcount++;			/* temprorarily allow replays */
			addMessage(1, "Increasing maxplaycount to %i (loop)", pcount);
		}
	}							/* while( valid != 3 ) */
	/*  *INDENT-OFF*  */
	addMessage(1, "[+] (%i/%i/%c) %5d %s",
			   (runner->flags & MP_FAV) ? runner->favpcount : runner->playcount,
			   pcount, flagToChar(runner->flags), runner->key, runner->display);
	/*  *INDENT-ON*  */
	/* count again in case this is a favourite */
	return appendToPL(runner, pl, -1);
}

/**
 * checks the current playlist.
 * If there are more than 10 previous titles, those get pruned.
 * While there are less that 10 next titles, titles will be added.
 */
void plCheck(int del) {
	int cnt = 0;
	mpplaylist_t *pl;
	mpplaylist_t *buf;

	/* make sure the playlist is not modifid elsewhere right now */
	lockPlaylist();

	pl = getConfig()->current;
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
		if (del != 0) {
			while (pl->prev != NULL) {
				pl = pl->prev;
			}
		}

		/* go through end of the playlist and clean up underway */
		while (pl->next != NULL) {
			/* clean up on the way to remove DNP marked or deleted files?
			 * there /should/ not be any doublets here, but it does not hurt
			 * to check those too */
			if (del != 0) {
				if ((pl->title->flags & (MP_DNP | MP_DBL))
					|| (!mp3Exists(pl->title))) {
					/* title leaves the playlist so it must be unmarked */
					pl->title->flags &= ~MP_MARK;
					buf = pl->next;
					if (pl->prev != NULL) {
						pl->prev->next = pl->next;
					}
					pl->next->prev = pl->prev;

					/* the current title has become invalid, erk.. */
					if (pl == getConfig()->current) {
						/* fake being at the previous title so the next title is not
						 * skipped */
						if (buf->prev != NULL) {
							getConfig()->current = buf->prev;
						}
						else {
							/* no previous title! Do the next best thing then */
							getConfig()->current = buf;
						}
					}
					free(pl);
					pl = buf;
				}
				else {
					pl = pl->next;
				}
			}
			else {
				pl = pl->next;
			}
		}

		/* Done cleaning, now start pruning */
		/* truncate playlist title history to 10 titles */
		cnt = 0;
		pl = getConfig()->current;
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
			/* unmark title as it leaves the playlist */
			buf->title->flags &= ~MP_MARK;
			free(buf);
			buf = pl;
		}

		/* Count titles to come */
		cnt = 0;
		pl = getConfig()->current;
		while (pl->next != NULL) {
			pl = pl->next;
			cnt++;
		}
	}

	/* fill up the playlist with new titles */
	while (cnt < 10) {
		if (getConfig()->current == NULL) {
			getConfig()->current = addNewTitle(NULL, getConfig()->root);
		}
		else {
			addNewTitle(getConfig()->current, getConfig()->root);
		}
		cnt++;
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
	int num, i;

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
void dumpInfo(mptitle_t * root, int smooth) {
	mptitle_t *current = root;
	unsigned maxplayed = 0;
	unsigned pl = 0;
	unsigned dnp = 0;
	unsigned dbl = 0;
	unsigned fav = 0;
	unsigned marked = 0;
	unsigned numtitles = 0;
	unsigned fixed = 0;

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
		if (current->flags & MP_MARK) {
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
		addMessage(0, "%5i marked", marked);
	addMessage(0, "-- Playcount --");

	while (pl <= maxplayed) {
		unsigned int pcount = 0;
		unsigned int dcount = 0;
		unsigned int dblcnt = 0;
		unsigned int favcnt = 0;
		unsigned int markcnt = 0;
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
				if (current->flags & MP_MARK) {
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
		/* normal output and forward to next count */
		else if (pcount > 0) {
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

			if (favcnt || dcount)
				if (favcnt == pcount)
					addMessage(0, "%s - allfav", line);
				else if (dcount == pcount)
					addMessage(0, "%s - alldnp", line);
				else if (favcnt == 0)
					if (dblcnt == 0)
						addMessage(0, "%s - %i dnp", line, dcount);
					else
						addMessage(0, "%s - %i/%i dnp", line, dcount, dblcnt);
				else if (dcount == 0)
					addMessage(0, "%s - %i fav", line, favcnt);
				else if (dblcnt == 0)
					addMessage(0, "%s - %i dnp / %i fav", line, dcount,
							   favcnt);
				else
					addMessage(0, "%s - %i/%i dnp / %i fav", line, dcount,
							   dblcnt, favcnt);
			else
				addMessage(0, "%s", line);
		}
		pl++;
	}							/* while pl < maxplay */

	if (fixed) {
		dbWrite(1);
	}
}

/**
 * Copies the given title to the target and turns the name into 'track###.mp3'
 * with ### being the index.
 * Returns -1 when the target runs out of space.
 */
static int copyTitle(mptitle_t * title, const char *target,
					 const unsigned int index) {
	int in = -1, out = -1, rv = -1;
	size_t len;
	char filename[MAXPATHLEN];
	struct stat st;

	snprintf(filename, MAXPATHLEN, "%strack%03i.mp3", target, index);

	in = open(title->path, O_RDONLY);

	if (-1 == in) {
		addMessage(0, "Couldn't open %s for reading", title->path);
		goto cleanup;
	}

	if (-1 == fstat(in, &st)) {
		addMessage(0, "Couldn't stat %s", title->path);
		goto cleanup;
	}
	len = st.st_size;

	out = open(filename, O_CREAT | O_WRONLY | O_EXCL, 00544);

	if (-1 == out) {
		addMessage(0, "Couldn't open %s for writing", filename);
		goto cleanup;
	}

	addMessage(1, "Copy %s to %s", title->display, filename);

	rv = sendfile(out, in, NULL, len);
	if (rv == -1) {
		if (errno == EOVERFLOW) {
			addMessage(0, "Target ran out of space!");
		}
		else {
			addMessage(0, "Could not copy %s", title->path);
		}
	}

  cleanup:
	if (in != -1) {
		close(in);
	}
	if (out != -1) {
		close(out);
	}

	return rv;
}

/**
 * copies the titles in the current playlist onto the target
 * only favourites are copied
 *
 * TODO this does not make any sense with the new playlist structure
 */
int fillstick(mptitle_t * root, const char *target) {
	unsigned int index = 0;
	mptitle_t *current;

	current = root;

	do {
		activity(0, "Copy title #%03i", index);
		if (current->flags & MP_FAV) {
			if (copyTitle(current, target, index++) == -1) {
				break;
			}
		}
		current = current->next;
	}
	while (current != root);

	addMessage(0, "Copied %i titles to %s", index, target);
	return index;
}

int addRangePrefix(char *line, mpcmd_t cmd) {
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

/**
 * wrapper to handle FAV and DNP. Uses the given title and the range
 * definitions in cmd to create the proper config line and immediately
 * applies it to the current database and playlist
 */
int handleRangeCmd(mptitle_t * title, mpcmd_t cmd) {
	char line[MAXPATHLEN + 2];
	marklist_t *buff, *list;
	int cnt = -1;
	mpconfig_t *config = getConfig();

	if (addRangePrefix(line, cmd) == 0) {
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

		buff = list;
		while (buff != NULL) {
			if (strcmp(line, buff->dir) == 0) {
				addMessage(0, "%s already in list!", line);
				return -1;
			}
			buff = buff->next;
		}

		buff = (marklist_t *) falloc(1, sizeof (marklist_t));
		strcpy(buff->dir, line);
		buff->next = NULL;

		if (list == NULL) {
			if (MPC_CMD(cmd) == mpc_fav) {
				config->favlist = buff;
			}
			else {
				config->dnplist = buff;
			}
		}
		else {
			while (list->next != NULL) {
				list = list->next;
			}
			list->next = buff;
		}

		addToList(buff->dir, cmd);
		if (MPC_CMD(cmd) == mpc_fav) {
			cnt = applyFAVlist(buff);
		}
		else if (MPC_CMD(cmd) == mpc_dnp) {
			cnt = applyDNPlist(buff, 0);
		}
	}

	return cnt;
}

/**
 * Adds a title to the global doublet list
 */
int handleDBL(mptitle_t * title) {
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
