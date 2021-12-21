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

#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include "mpalsa.h"
#include "database.h"
#include "player.h"
#include "config.h"
#include "mpinit.h"
#include "mpcomm.h"

#define MPV 10

static pthread_mutex_t _pcmdlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _pcmdcond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t _asynclock = PTHREAD_MUTEX_INITIALIZER;

/**
 * adds searchresults to the playlist
 * range - title-display/artist/album
 * arg - either a title key or a string
 * insert - play next or append to the end of the playlist
 */
static int32_t playResults(mpcmd_t range, const char *arg,
						   const int32_t insert) {
	mpconfig_t *config = getConfig();
	mpplaylist_t *pos = config->current;
	mpplaylist_t *res = config->found->titles;
	mptitle_t *title = NULL;
	int32_t key = atoi(arg);

	/* insert results at current pos or at the end? */
	if ((pos != NULL) && (insert == 0)) {
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
				pos = addToPL(res->title, pos, 0);
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
		pos = addToPL(title, pos, 0);
		if (config->current == NULL) {
			config->current = pos;
		}

		notifyChange(MPCOMM_TITLES);
		return 1;
	}

	addMessage(0, "Range not supported!");
	return 0;
}

/*
 * reset controller on restart
 */
void unlockController( void ) {
	mpconfig_t *control=getConfig();
	pthread_mutex_unlock(&_asynclock);
	/* drain command queue */
	while (pthread_mutex_trylock(&_pcmdlock) == EBUSY) {
		control->command = mpc_idle;
		addMessage(0, "Draining queue");
		pthread_cond_signal(&_pcmdcond);
		/* make sure to be slower than anyone else */
		usleep(500);
	}
	pthread_mutex_unlock(&_pcmdlock);
}

/**
 * returns TRUE when no asynchronous operation is running but does not
 * block on async operations.
 */
int32_t asyncTest() {
	int32_t ret = 0;

	if (pthread_mutex_trylock(&_asynclock) != EBUSY) {
		addMessage(MPV + 1, "Locking for %s/%s",
				   mpcString(getConfig()->command),
				   mpcString(getConfig()->status));
		ret = 1;
	}
	else {
		addMessage(0, "Player is already locked!");
	}
	return ret;
}

static int32_t checkPasswd(char *pass) {
	if (asyncTest()) {
		if (pass && !strcmp(getConfig()->password, pass)) {
			return 1;
		}
		addMessage(MPV + 1, "Unlocking player after wrong password");
		/* unlock mutex locked in asyncTest() */
		pthread_mutex_unlock(&_asynclock);
		/* TODO this may be potentially dangerous! */
		unlockClient(-1);
		addMessage(-1, "Wrong password!");
	}
	return 0;
}

/**
 * asnchronous functions to run in the background and allow updates being sent to the
 * client
 */
static void *plCheckDoublets(void *arg) {
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;
	int32_t i;

	addMessage(0, "Checking for doublets..");
	/* update database with current playcount etc */
	dbWrite(0);

	i = dbNameCheck();
	if (i > 0) {
		addMessage(0, "Marked %i doublets", i);
		applyLists(0);
		plCheck(1);
	}
	else {
		addMessage(0, "No doublets found");
	}
	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

static void *plDbClean(void *arg) {
	mpconfig_t *control = getConfig();
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;
	int32_t i;
	int32_t changed = 0;

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
		changed=1;
	}
	else {
		addMessage(0, "No titles to be added");
	}

	if (changed) {
		dbWrite(1);
		setArtistSpread();
		plCheck(0);
	}

	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

static void *plDbFix(void *arg) {
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;

	addMessage(0, "Database smooth");
	dumpInfo(1);
	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

static void *plDbInfo(void *arg) {
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;

	addMessage(0, "Database Info");
	dumpInfo(0);
	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

/* simple wrapper to run setProfile as an own thread */
static void *plSetProfile(void *arg) {
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;

	setProfile(NULL);
	pthread_mutex_unlock(lock);
	return NULL;
}

/**
 * run the given command asynchronously to allow updates during execution
 * if channel is != -1 then playing the song will be paused during execution
 */
static void asyncRun(void *cmd(void *)) {
	pthread_t pid;

	if (pthread_create(&pid, NULL, cmd, &_asynclock) < 0) {
		addMessage(0, "Could not create async thread!");
	}
}

/**
 * sends a command to the player
 * also makes sure that commands are not overwritten
 */
void setCommand(mpcmd_t rcmd, char *arg) {
	mptitle_t *ctitle = NULL;	/* the title commands should use */
	mpconfig_t *config = getConfig();
	char *tpos;
	uint32_t insert = 0;
	int32_t i;
	int32_t profile;
	mpcmd_t cmd;

	if ((rcmd == mpc_idle) ||
		(config->status == mpc_quit) || (config->status == mpc_reset)) {
		return;
	}

	if (rcmd == mpc_reset) {
		killPlayers(1);
		return;
	}

	if (pthread_mutex_trylock(&_pcmdlock) == EBUSY) {
		/* Wait until someone unlocks */
		addMessage(MPV + 1, "%s waiting to be set", mpcString(rcmd));
		pthread_mutex_lock(&_pcmdlock);
	}

	/* wait for the last command to be handled */
	while (config->command != mpc_idle) {
		addMessage(MPV + 1, "%s blocked on %s/%s", mpcString(rcmd),
				   mpcString(config->command), mpcString(config->status));
		pthread_cond_wait(&_pcmdcond, &_pcmdlock);
		addMessage(MPV + 1, "unblocked on %s/%s", mpcString(config->command),
				   mpcString(config->status));
	}

	config->command = rcmd;
	/* player is being reset or about to quit - do not handle any commands */
	if ((config->status == mpc_reset) && (config->status == mpc_quit)) {
		addMessage(0, "%s dropped for %s!", mpcString(rcmd),
				   mpcString(config->status));
		return;
	}

	addMessage(MPV + 1, "Setting %s..", mpcString(rcmd));
	cmd = MPC_CMD(rcmd);

	/* get the target title for fav and dnp commands */
	if (config->current != NULL) {
		ctitle = config->current->title;
		if ((cmd == mpc_fav) || (cmd == mpc_dnp)) {
			if (arg != NULL) {
				/* todo: check if config->argument is a number */
				if (MPC_EQDISPLAY(rcmd)) {
					ctitle = getTitleByIndex(atoi(arg));
				}
				else {
					ctitle = getTitleForRange(rcmd, arg);
				}
				/* someone is testing parameters, mh? */
				if (ctitle == NULL) {
					addMessage(0, "Nothing matches %s!", arg);
				}
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
			plCheck(0);
			config->status = mpc_start;
			sendplay();
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
					config->status=mpc_start;
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
			plCheck(0);
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
		else if (asyncTest()) {
			i=-1;
			if (arg != NULL) {
				i = -atoi(arg);
			}
			setOrder(i);
			toPlayer(0, "STOP\n");
			pthread_mutex_unlock(&_asynclock);
		}
		break;

	case mpc_next:
		/* This /may/ make sense on streamlists but so far a stream has no
		 * previous or next title */
		if (config->mpmode & PM_STREAM)
			break;
		if ((config->current != NULL) && asyncTest()) {
			i = 1;
			if (arg != NULL) {
				i = atoi(arg);
			}
			else {
				setSkipped();
			}
			setOrder(i);
			toPlayer(0, "STOP\n");
			pthread_mutex_unlock(&_asynclock);
		}
		break;

	case mpc_doublets:
		if (config->mpmode & PM_STREAM)
			break;
		if (checkPasswd(arg)) {
			asyncRun(plCheckDoublets);
		}
		break;

	case mpc_dbclean:
		if (config->mpmode & PM_STREAM)
			break;
		if (checkPasswd(arg)) {
			asyncRun(plDbClean);
		}
		break;

	case mpc_stop:
		addMessage(MPV + 1, "mpc_stop (%s)", mpcString(config->status));
		stopPlay();
		break;

	case mpc_dnp:
		if (config->mpmode & PM_STREAM)
			break;
		if ((ctitle != NULL) && asyncTest()) {
			if (ctitle == config->current->title) {
				/* current title is affected, play the next one */
				setOrder(0);
				toPlayer(0, "STOP\n");
			}
			handleRangeCmd(ctitle, rcmd);
			setArtistSpread();
			plCheck(1);
			pthread_mutex_unlock(&_asynclock);
		}
		break;

	case mpc_fav:
		if (config->mpmode & PM_STREAM)
			break;
		if ((ctitle != NULL) && asyncTest()) {
			handleRangeCmd(ctitle, rcmd);
			notifyChange(MPCOMM_TITLES);
			pthread_mutex_unlock(&_asynclock);
			/* when playing only favourites, adding titles can make
			 * a difference */
			if(getFavplay()) {
				setArtistSpread();
			}
		}
		break;

	case mpc_repl:
		if (config->mpmode & PM_STREAM)
			break;
		toPlayer(0, "JUMP 0\n");
		config->percent = 0;
		config->playtime = 0;
		break;

	case mpc_quit:
		/* The player does not know about the main App so anything setting
		 * mcp_quit MUST make sure that the main app terminates as well ! */
		if (checkPasswd(arg)) {
			config->status = mpc_quit;
			pthread_mutex_unlock(&_asynclock);
		}
		break;

	case mpc_profile:
		if (asyncTest()) {
			if (arg == NULL) {
				addMessage(-1, "No profile given!");
			}
			else {
				profile = atoi(arg);
				if ((profile != 0) && (profile != config->active)) {
					if (!(config->mpmode & (PM_STREAM | PM_DATABASE))) {
						wipeTitles(config->root);
					}
					/* switching channels started */
					config->mpmode |= PM_SWITCH;
					/* write database if needed */
					dbWrite(0);
					if (config->active < 0) {
						config->stream[(-config->active) - 1]->volume =
							config->volume;
					}
					else if (config->active > 0) {
						config->profile[config->active - 1]->volume =
							config->volume;
					}
					config->active = profile;
					asyncRun(plSetProfile);
				}
				else {
					addMessage(0, "Invalid profile %i", profile);
					pthread_mutex_unlock(&_asynclock);
				}
			}
		}
		break;

	case mpc_newprof:
		if ((config->current != NULL) && asyncTest()) {
			if (arg == NULL) {
				addMessage(-1, "No name given!");
			}
			/* save the current stream */
			else if (config->active == 0) {
				config->streams++;
				config->stream =
					(profile_t **) frealloc(config->stream,
											config->streams *
											sizeof (profile_t *));
				config->stream[config->streams - 1] =
					createProfile(arg, config->streamURL, 0,
								  config->volume);
				config->active = -(config->streams);
			}
			/* just add argument as new profile */
			else {
				config->profiles++;
				config->profile =
					(profile_t **) frealloc(config->profile,
											config->profiles *
											sizeof (profile_t *));
				config->profile[config->profiles - 1] =
					createProfile(arg, NULL, 0,
								  config->volume);
			}
			writeConfig(NULL);
			pthread_mutex_unlock(&_asynclock);
			notifyChange(MPCOMM_CONFIG);
		}
		break;

	case mpc_clone:
		if ((config->current != NULL) && asyncTest()) {
			if (arg == NULL) {
				addMessage(-1, "No profile given!");
			}
			/* only clone profiles */
			else if (config->active > 0) {
				config->profiles++;
				config->profile =
					(profile_t **) frealloc(config->profile,
											config->profiles *
											sizeof (profile_t *));
				config->profile[config->profiles - 1] =
					createProfile(arg, NULL, 0,
								  config->volume);
				config->active = config->profiles;
				writeList(mpc_fav);
				writeList(mpc_dnp);

				writeConfig(NULL);
				notifyChange(MPCOMM_CONFIG);
			}
			pthread_mutex_unlock(&_asynclock);
		}
		break;

	case mpc_remprof:
		if (arg == NULL) {
			addMessage(-1, "No profile given!");
		}
		else {
			if (asyncTest()) {
				profile = atoi(arg);
				if (profile > 0) {
					if (profile == 1) {
						addMessage(-1, "mixplay cannot be removed.");
					}
					else if (profile == config->active) {
						addMessage(-1, "Cannot remove active profile!");
					}
					else if (profile > config->profiles) {
						addMessage(-1, "Profile #%i does not exist!",
								   profile);
					}
					else {
						free(config->profile[profile - 1]);
						for (i = profile; i < config->profiles; i++) {
							config->profile[i - 1] = config->profile[i];
							if (i == config->active) {
								config->active = config->active - 1;
							}
						}
						config->profiles--;
						writeConfig(NULL);
					}
				}
				else if (profile < 0) {
					profile = (-profile);
					if (profile > config->streams) {
						addMessage(-1, "Stream #%i does not exist!",
								   profile);
					}
					else if (-profile == config->active) {
						addMessage(-1, "Cannot remove active profile!");
					}
					else {
						freeProfile(config->stream[profile - 1]);
						for (i = profile; i < config->streams; i++) {
							config->stream[i - 1] = config->stream[i];
							if (i == config->active) {
								config->active = config->active - 1;
							}
						}
						config->streams--;
						config->stream =
							(profile_t **) frealloc(config->stream,
													config->streams *
													sizeof (profile_t *));

						writeConfig(NULL);
					}
				}
				else {
					addMessage(-1, "Cannot remove empty profile!");
				}
				pthread_mutex_unlock(&_asynclock);
			}
		}
		break;

	case mpc_path:
		if ((config->current != NULL) && asyncTest()) {
			if (arg == NULL) {
				addMessage(-1, "No path given!");
			}
			else {
				if (setArgument(arg)) {
					config->active = 0;
					stopPlay();
					sendplay();
				}
			}
			pthread_mutex_unlock(&_asynclock);
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
		if (config->mpmode & PM_STREAM)
			break;
		if ((arg) && checkPasswd(arg)) {
			asyncRun(plDbFix);
		}
		else if (asyncTest()) {
			asyncRun(plDbInfo);
		}
		break;

		/* this is kinda ugly as the logic needs to be all over the place
		 * the state is initially set to busy in the server code, then the server
		 * waits for the search() called here to set the state to send thus
		 * unlocking the server to send the reply and finally sets it to idle
		 * again, unlocking the player */
	case mpc_search:
		if (config->mpmode & PM_STREAM)
			break;
		/* if we mix two searches we're in trouble!
		 * this duplicates the check in mpserver, so it should never happen */
		assert(config->found->state == mpsearch_idle);

		if (arg == NULL) {
			if (MPC_ISARTIST(rcmd)) {
				arg = strdup(ctitle->artist);
			}
			else {
				arg = strdup(ctitle->album);
			}
		}
		if (search(arg, MPC_MODE(rcmd)) == -1) {
			addMessage(0, "Too many titles found!");
		}
		sfree(&arg);

		break;

	case mpc_insert:
		if (config->mpmode & PM_STREAM)
			break;
		insert = 1;
		/* fallthrough */

	case mpc_append:
		if (config->mpmode & PM_STREAM)
			break;
		if (arg != NULL) {
			playResults(MPC_RANGE(rcmd), arg,
						insert);
		}
		insert = 0;
		break;

	case mpc_setvol:
		if (arg != NULL) {
			setVolume(atoi(arg));
		}
		break;

	case mpc_smode:
		if (config->mpmode & PM_STREAM)
			break;
		config->searchDNP = ~(config->searchDNP);
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
			config->current =
				remFromPLByKey(atoi(arg));
			plCheck(0);
		}
		break;

	case mpc_mute:
		toggleMute();
		break;

	case mpc_favplay:
		if (config->mpmode & PM_STREAM)
			break;
		if (asyncTest()) {
			if (countflag(MP_FAV) < 21) {
				addMessage(-1,
						   "Need at least 21 Favourites to enable Favplay.");
				break;
			}
			if (toggleFavplay()) {
				addMessage(MPV + 1, "Enabling Favplay");
			}
			else {
				addMessage(MPV + 1, "Disabling Favplay");
			}
			cleanTitles(0);
			writeConfig(NULL);
			setArtistSpread();
			plCheck(0);
			sendplay();
			pthread_mutex_unlock(&_asynclock);
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
		/* read current Hardware volume in case it changed externally
		 * don't read before arg is NULL as someone may be
		 * trying to set the volume right now */
		if ((arg == NULL) && (config->volume != -1)) {
			config->volume = getVolume();
		}
		break;

	case mpc_reset:
		addMessage(-1, "Force restart!");
		config->status = mpc_reset;
		 killPlayers(1);
		return;

	default:
		addMessage(0, "Received illegal command %i", rcmd);
		break;
	}

	config->command = mpc_idle;
	pthread_cond_signal(&_pcmdcond);

	pthread_mutex_unlock(&_pcmdlock);
}
