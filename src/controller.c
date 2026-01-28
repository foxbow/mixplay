/*
 * controller.c
 *
 * consists mainly of setCommand() the function to control the player through
 * mpc_* commands. Split from the player loop to clean up code and lower the
 * likeliness of deadlocks while playing.
 *
 *  Created on: 16.12.2021
 *	  Author: bweber
 */

#include <errno.h>
#include <unistd.h>

#include "mpalsa.h"
#include "database.h"
#include "player.h"
#include "config.h"
#include "mpinit.h"
#include "mpcomm.h"

#define MPV 10

/* make sure only one thread at a time sets a command */
static pthread_mutex_t _pcmdlock = PTHREAD_MUTEX_INITIALIZER;

/**
 * adds searchresults to the playlist
 * range - title-display/artist/album
 * arg - either a title key or a string
 * insert - play next or append to the end of the playlist
 */
static int32_t playResults(mpcmd_t range, const char *arg, const bool insert) {
	mpconfig_t *config = getConfig();
	mpplaylist_t *pos = config->current;
	mpplaylist_t *res = config->found->titles;
	mptitle_t *title = NULL;
	int32_t key = atoi(arg);

	/* insert results at current pos or at the end? */
	if ((pos != NULL) && (!insert)) {
		while (pos->next != NULL) {
			pos = pos->next;
		}
	}

	if ((range == mpc_title) || (range == mpc_display)) {
		/* Play the current resultlist */
		if (key == 0) {
			/* should not happen but better safe than sorry! */
			if (config->found->tnum == 0) {
				addMessage(0, "No results to be added!");
				return 0;
			}
			if (config->found->titles == NULL) {
				addMessage(0, "%i titles found but none in list!",
						   config->found->tnum);
				return 0;
			}

			while (res != NULL) {
				pos = addToPL(res->title, pos, false);
				if (config->current == NULL) {
					config->current = pos;
				}
				res = res->next;
			}

			notifyChange(MPCOMM_TITLES);
			return config->found->tnum;
		}

		/* play only one */
		title = getTitleByIndex(key);
		if (title == NULL) {
			addMessage(0, "No title with key %i!", key);
			return 0;
		}
		/*
		 * Do not touch marking, we searched the title so it's playing out of
		 * order. It has been played before? Don't care, we want it now and it
		 * won't come back! It's not been played before? Then play it now and
		 * whenever it's time comes.
		 */
		pos = addToPL(title, pos, false);
		if (config->current == NULL) {
			config->current = pos;
		}

		notifyChange(MPCOMM_TITLES);
		return 1;
	}

	addMessage(0, "Range not supported!");
	return 0;
}

static bool checkPasswd(char *pass) {
	if (!pass) return false;
	if (!strcmp(getConfig()->password, pass)) return true;
	addMessage(-1, "Wrong password!");
	return false;
}

/*
 * returns the current title or NULL if the playlist has not been initialized
 * yet
 */
static mptitle_t *getCurrentTitle() {
	if (getConfig()->current != NULL) {
		return getConfig()->current->title;
	}
	return NULL;
}

/**
 * to be called after removing titles, marking them DNP or as DBL.
 **/
static void checkAfterRemove(mptitle_t * ctitle) {
	/* clean up the playlist without adding new titles */
	plCheck(false);
	/* has the current title changed? Then send a replay to play the new
	 * current title. This may lead to a replay if the title changed during
	 * plcheck() but the effort to avoid this is larger than the expected
	 * impact */
	if (ctitle != getConfig()->current->title) {
		setOrder(0);
		toPlayer(0, "STOP\n");
	}
	setArtistSpread();
	/* fill up the playlist */
	plCheck(true);
}

/**
 * asnchronous functions to run in the background and allow updates being sent to the
 * client
 */
static void *plCheckDoublets(void *cidin) {
	int32_t cid = (int32_t)(int64_t)cidin;
	int32_t i;
	mptitle_t *ctitle = getCurrentTitle();

	lockClient(cid);
	addMessage(0, "Checking for doublets..");
	/* update database with current playcount etc */
	dbWrite(0);

	i = dbNameCheck();
	if (i > 0) {
		addMessage(0, "Marked %i doublets", i);
		applyLists(0);
		checkAfterRemove(ctitle);
	}
	else {
		addMessage(0, "No doublets found");
	}
	setTnum();
	unlockClient(cid);
	return NULL;
}

static void *plDbClean(void *cidin) {
	int32_t cid = (int32_t)(int64_t)cidin;
	mpconfig_t *control = getConfig();
	mptitle_t *ctitle = getCurrentTitle();
	int32_t i;
	int32_t changed = 0;

	lockClient(cid);
	addMessage(0, "Database Cleanup");

	/* update database with current playcount etc */
	dbWrite(0);

	addMessage(0, "Checking for deleted titles..");
	i = dbCheckExist();

	if (i > 0) {
		addMessage(0, "Removed %i titles", i);
		changed = 1;
	}
	else {
		addMessage(0, "No titles removed");
	}

	addMessage(0, "Checking for new titles..");
	i = dbAddTitles(control->musicdir);

	if (i > 0) {
		addMessage(0, "Added %i new titles", i);
		changed |= 2;
	}
	else {
		addMessage(0, "No titles to be added");
	}

	if (changed) {
		dbWrite(1);
		setArtistSpread();
		if (changed & 1) {
			checkAfterRemove(ctitle);
		}
	}

	setTnum();
	unlockClient(cid);
	return NULL;
}

static void *plDbFix(void *cidin) {
	int32_t cid = (int32_t)(int64_t)cidin;
	lockClient(cid);
	addMessage(0, "Database smooth");
	dumpInfo(true);
	setTnum();
	unlockClient(cid);
	return NULL;
}

static void *plDbInfo(void *cidin) {
	int32_t cid = (int32_t)(int64_t)cidin;
	lockClient(cid);
	addMessage(0, "Database Info");
	dumpState();
	unlockClient(cid);
	return NULL;
}

/* simple wrapper to run setProfile as an own thread */
static void *plSetProfile(void *cidin) {
	int32_t cid = (int32_t)(int64_t)cidin;
	lockClient(cid);
	setProfile(NULL);
	unlockClient(cid);
	return NULL;
}

/**
 * run the given command asynchronously to allow updates during execution
 * if channel is != -1 then playing the song will be paused during execution
 */
static void asyncRun(void *cmd(void *), int32_t cid) {
	pthread_t pid;

	if (pthread_create(&pid, NULL, cmd, (void *)(uint64_t)cid) < 0) {
		addMessage(0, "Could not create async thread!");
	}
	else {
		pthread_setname_np(pid, "tmpcmdhndlr");
	}
}

/**
 * sends a command to the player
 * 
 * this has a staggered lock. 
 */
void setCommand(mpcmd_t rcmd, char *arg, int32_t cid) {
	mptitle_t *ctitle = getCurrentTitle();	/* the title commands should use */
	mpconfig_t *config = getConfig();
	char *tpos;
	bool insert = false;
	int32_t order;
	uint32_t profileid;
	mpcmd_t cmd;

	if ((rcmd == mpc_idle) ||
		(config->status == mpc_quit) || (config->status == mpc_reset)) {
		return;
	}

	if (rcmd == mpc_reset) {
		config->status = mpc_reset;
		killPlayers(1);
		return;
	}

	/* QUIT has precedence over all */
	if (rcmd == mpc_quit) {
		if (arg && !strcmp(getConfig()->password, arg)) {
			config->status = mpc_quit;
			killPlayers(0);
		}
		return;
	}

	/* lock the command handling */
	pthread_mutex_lock(&_pcmdlock);

	/* a quit or reset came in while the mutex was blocked, so forget about
	 * everything until we had a clean restart */
	if ((config->status == mpc_quit) || (config->status == mpc_reset)) {
		pthread_mutex_unlock(&_pcmdlock);
		return;
	}

	addMessage(MPV + 1, "Setting %s..", mpcString(rcmd));
	cmd = MPC_CMD(rcmd);

	/* get the target title for fav and dnp commands */
	if ((cmd == mpc_fav) || (cmd == mpc_dnp)) {
		if (arg != NULL) {
			if (MPC_EQDISPLAY(rcmd)) {
				ctitle = getTitleByIndex(atoi(arg));
			}
			else {
				ctitle = getTitleForRange(rcmd, arg);
			}
			/* someone is testing parameters, mh? */
			if (ctitle == NULL) {
				addMessage(0, "Nothing matches %s!", arg);
				pthread_mutex_unlock(&_pcmdlock);
				return;
			}
		}
	}

	switch (cmd) {
	case mpc_start:
		addMessage(MPV + 1, "mpc_start (%s)", mpcString(config->status));
		if (config->status == mpc_start) {
			addMessage(MPV + 1, "Already starting!");
		}
		else {
			config->status = mpc_start;
			plCheck(true);

			if (config->stop) {
				activity(0, "Welcome");
				config->stop = false;
			}
			else {
				sendplay();
			}
			notifyChange(MPCOMM_CONFIG);
		}
		break;

	case mpc_play:
		addMessage(MPV + 1, "mpc_play (%s)", mpcString(config->status));
		/* has the player been properly initialized yet? */
		if (config->status != mpc_start) {
			/* simple stop */
			if (config->status == mpc_play) {
				pausePlay();
			}
			/* play */
			else {
				/* play stream */
				if (config->mpmode & PM_STREAM) {
					/* wake up UI in case it's in sleep mode */
					config->status = mpc_start;
					sendplay();
				}
				/* unpause on normal play */
				else {
					toPlayer(0, "PAUSE\n");
				}
			}
		}
		/* initialize player after startup */
		/* todo: what happens if the user sends a play during startup? */
		else {
			if (config->current != NULL) {
				addMessage(MPV + 1, "Autoplay..");
				sendplay();
			}
		}
		break;

	case mpc_prev:
		/* This /may/ make sense on streamlists but so far a stream has no
		 * previous or next title */
		if (config->mpmode & PM_STREAM)
			break;
		if (config->playtime > 3) {
			toPlayer(0, "JUMP 0\n");
			config->percent = 0;
			config->playtime = 0;
		}
		else {
			/* make sure we do not interrupt actions on the playlist */
			lockClient(cid); 
			order = -1;
			if (arg != NULL) {
				order = -atoi(arg);
			}
			setOrder(order);
			toPlayer(0, "STOP\n");
			unlockClient(cid);
		}
		break;

	case mpc_next:
		/* This /may/ make sense on streamlists but so far a stream has no
		 * previous or next title */
		if (config->mpmode & PM_STREAM)
			break;
		if (config->current != NULL) {
			/* make sure we do not interrupt actions on the playlist */
			lockClient(cid); 
			order = 1;
			if (arg != NULL) {
				order = atoi(arg);
			}
			else {
				setSkipped();
			}
			setOrder(order);
			toPlayer(0, "STOP\n");
			unlockClient(cid);
		}
		break;

	case mpc_doublets:
		if (checkPasswd(arg)) {
			asyncRun(plCheckDoublets, cid);
		}
		break;

	case mpc_dbclean:
		if (checkPasswd(arg)) {
			asyncRun(plDbClean, cid);
		}
		break;

	case mpc_stop:
		addMessage(MPV + 1, "mpc_stop (%s)", mpcString(config->status));
		stopPlay();
		break;

	case mpc_dnp:
	case mpc_fav:
		if (config->mpmode & PM_STREAM)
			break;
		lockClient(cid);
		/* remember the current title so checkAfterRemove() can find out if
			* it has changed */
		mptitle_t *check = getCurrentTitle();

		/* The selected title may already be explicitly marked as DNP or FAV so
			* check if it needs to be removed from the other list. The last choice
			* shall have highest priority */
		delTitleFromOtherList(rcmd, ctitle);
		handleRangeCmd(rcmd, ctitle);
		if (cmd == mpc_dnp) {
			checkAfterRemove(check);
		}
		notifyChange(MPCOMM_TITLES);
		if (getFavplay()) {
			setArtistSpread();
		}
		setTnum();
		unlockClient(cid);
		break;

	case mpc_repl:
		if (config->mpmode & PM_STREAM)
			break;
		toPlayer(0, "JUMP 0\n");
		config->percent = 0;
		config->playtime = 0;
		break;

	case mpc_profile:
		if (arg == NULL) {
			addMessage(-1, "No profile given!");
		}
		else {
			profileid = atoi(arg);
			if ((profileid != 0) && (profileid != config->active)) {
				/* TODO: should this be locked too? */
				if (!(config->mpmode & (PM_STREAM | PM_DATABASE))) {
					wipeTitles(config->root);
				}
				/* switching channels started */
				config->mpmode |= PM_SWITCH;
				/* write database if needed */
				dbWrite(0);
				/* do not touch volume when muted or lineout is active */
				if ((config->active != 0) && (config->volume > 0)) {
					getProfile(config->active)->volume = config->volume;
				}
				config->active = profileid;
				asyncRun(plSetProfile, cid);
			}
			else {
				addMessage(0, "Invalid profile %i", profileid);
			}
		}
		break;

	case mpc_newprof:
		if (arg == NULL) {
			addMessage(-1, "No name given!");
		}
		else if (strchr(arg, '/')) {
			addMessage(-1, "Illegal profile name.<br>Don't use '/'!");
		}
		else if (config->current != NULL) {
			lockClient(cid);
			addProfile(arg, config->streamURL, true);
			writeConfig(NULL);
			unlockClient(cid);
			notifyChange(MPCOMM_CONFIG);
		}
		break;

	case mpc_clone:
		if (arg == NULL) {
			addMessage(-1, "No profile given!");
		}
		else if ((config->current != NULL)) {
			/* only clone profiles */
			if (!isStreamActive()) {
				lockClient(cid);
				if (copyProfile(config->active, arg) > 0) {
					writeList(mpc_fav);
					writeList(mpc_dnp);

					writeConfig(NULL);
					notifyChange(MPCOMM_CONFIG);
				}
				unlockClient(cid);
			}
		}
		break;

	case mpc_remprof:
		if (arg == NULL) {
			addMessage(-1, "No profile given!");
		}
		else {
			profileid = atoi(arg);
			int profileidx = getProfileIndex(profileid);

			if (profileidx != -1) {
				if (profileid == 1) {
					addMessage(-1, "mixplay cannot be removed.");
				}
				else if (profileid == config->active) {
					addMessage(-1, "Cannot remove active profile!");
				}
				else {
					lockClient(cid);
					freeProfile(config->profile[profileidx]);
					for (uint32_t i = profileidx + 1; i < config->profiles;
							i++) {
						config->profile[i - 1] = config->profile[i];
					}
					config->profiles--;
					config->profile =
						(profile_t **) frealloc(config->profile,
												config->profiles *
												sizeof (profile_t *));
					writeConfig(NULL);
					unlockClient(cid);
					notifyChange(MPCOMM_CONFIG);
				}
			}
			else {
				addMessage(-1, "Profile #%i does not exist!", profileid);
			}
		}
		break;

	case mpc_path:
		if (arg == NULL) {
			addMessage(-1, "No path given!");
		}
		else if (config->current != NULL) {
			lockClient(cid);
			if (setArgument(arg)) {
				config->active = 0;
				stopPlay();
				sendplay();
			}
			unlockClient(cid);
		}
		break;

	case mpc_ivol:
		adjustVolume(+VOLSTEP);
		break;

	case mpc_dvol:
		adjustVolume(-VOLSTEP);
		break;

	case mpc_bskip:
		if (config->mpmode & PM_STREAM)
			break;
		toPlayer(0, "JUMP -64\n");
		break;

	case mpc_fskip:
		if (config->mpmode & PM_STREAM)
			break;
		toPlayer(0, "JUMP +64\n");
		break;

	case mpc_dbinfo:
		asyncRun(plDbInfo, cid);
		break;

	case mpc_spread:
		if ((arg) && checkPasswd(arg)) {
			asyncRun(plDbFix, cid);
		}
		break;

		/* this is kinda ugly as the logic needs to be all over the place
		 * the state is initially set to busy in the server code, then the server
		 * waits for the search() called here to set the state to send thus
		 * unlocking the server to send the reply and finally sets it to idle
		 * again, unlocking the player */
	case mpc_search:
		{
			char *term = arg;

			/* if the term is unset but the fuzzy bit is set, return the last ten
			 * titles in the database */
			if ((term == NULL) && !MPC_ISRECENT(rcmd)) {
				if (MPC_ISARTIST(rcmd)) {
					term = ctitle->artist;
				}
				else {
					term = ctitle->album;
				}
			}

			if (config->found->cid > 0) {
				addMessage(-1, "Search still active!");
			}
			else {
				lockClient(cid);
				if (search(MPC_MODE(rcmd), term, cid) == -1) {
					addMessage(0, "Too many entries found!");
				}
				unlockClient(cid);
				notifyChange(MPCOMM_RESULT);
			}
		}

		break;

	case mpc_insert:
		if (config->mpmode & PM_STREAM)
			break;
		insert = true;
		/* fallthrough */

	case mpc_append:
		if (config->mpmode & PM_STREAM)
			break;
		if (arg != NULL) {
			playResults(MPC_RANGE(rcmd), arg, insert);
		}
		insert = false;
		break;

	case mpc_setvol:
		if (arg != NULL) {
			setVolume(atoi(arg));
		}
		break;

	case mpc_deldnp:
	case mpc_delfav:
		if (config->mpmode & PM_STREAM)
			break;
		if (arg != NULL) {
			delFromList(cmd, arg);
		}
		break;

	case mpc_remove:
		if (config->mpmode & PM_STREAM)
			break;
		if (arg != NULL) {
			config->current = remFromPLByKey(atoi(arg));
			checkAfterRemove(ctitle);
		}
		break;

	case mpc_mute:
		toggleMute();
		break;

	case mpc_favplay:
		if (config->mpmode & PM_STREAM)
			break;
		if (countflag(MP_FAV) < 21) {
			addMessage(-1,
						"Need at least 21 Favourites to enable Favplay.");
		}
		else {
			lockClient(cid);
			if (toggleFavplay()) {
				addMessage(MPV + 1, "Enabling Favplay");
			}
			else {
				addMessage(MPV + 1, "Disabling Favplay");
			}
			cleanTitles(false);
			writeConfig(NULL);
			setArtistSpread();
			plCheck(true);
			sendplay();
			unlockClient(cid);
		}

		break;

	case mpc_move:
		if (config->mpmode & PM_STREAM)
			break;
		if (arg != NULL) {
			tpos = strchr(arg, '/');
			if (tpos != NULL) {
				*tpos = 0;
				tpos++;
				moveTitleByIndex(atoi(arg), atoi(tpos));
			}
			else {
				moveTitleByIndex(atoi(arg), 0);
			}
			notifyChange(MPCOMM_TITLES);
		}
		break;

	case mpc_idle:
	case mpc_quit:
	case mpc_reset:
		/* handled above */
		break;


	default:
		addMessage(0, "Received illegal command %i", rcmd);
		break;
	}

	pthread_mutex_unlock(&_pcmdlock);
}
