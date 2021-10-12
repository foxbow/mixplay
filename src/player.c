/*
 * player.c
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
 */
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <sys/wait.h>

#include "mpalsa.h"
#include "utils.h"
#include "database.h"
#include "player.h"
#include "config.h"
#include "mpinit.h"
#include "mpcomm.h"

static pthread_mutex_t _pcmdlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t _pcmdcond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t _asynclock = PTHREAD_MUTEX_INITIALIZER;

/* clean up when switching mode
 * if flags is true, all title flags will be cleared, only
 * MP_DBL will be kept.
 */
static void cleanTitles(int flags) {
	mpconfig_t *control = getConfig();
	mptitle_t *runner = control->root;

	control->current =
		wipePlaylist(control->current, control->mpmode & PM_STREAM);
	/* this should even be assert() worthy */
	if (runner == NULL) {
		addMessage(-1, "Switching Favplay without active database!");
		return;
	}
	do {
		runner->favpcount = runner->playcount - 1;
		runner = runner->next;
		if (flags) {
			/* just keep MP_DBL */
			runner->flags &= MP_DBL;
		}
	} while (runner != control->root);
}

/**
 * returns TRUE when no asynchronous operation is running but does not
 * block on async operations.
 */
static int asyncTest() {
	int ret = 0;

	if (pthread_mutex_trylock(&_asynclock) != EBUSY) {
		addMessage(1, "Locking for %s/%s",
				   mpcString(getConfig()->command),
				   mpcString(getConfig()->status));
		ret = 1;
	}
	else {
		addMessage(0, "Player is already locked!");
	}
	return ret;
}

static int checkPasswd(void) {
	char *pass = getConfig()->argument;

	if (asyncTest()) {
		if (pass && !strcmp(getConfig()->password, pass)) {
			return 1;
		}
		addMessage(1, "Unlocking player after wrong password");
		/* unlock mutex locked in asyncTest() */
		pthread_mutex_unlock(&_asynclock);
		/* TODO this may be potentially dangerous! */
		unlockClient(-1);
		addMessage(-1, "Wrong password!");
	}
	return 0;
}

/**
 * sets the given stream
 */
void setStream(char const *const stream, char const *const name) {
	mpconfig_t *control = getConfig();

	lockPlaylist();
	control->current =
		wipePlaylist(control->current, control->mpmode & PM_STREAM);
	control->mpmode = PM_STREAM | PM_SWITCH;
	control->current = addPLDummy(control->current, "<waiting for info>");
	control->current = addPLDummy(control->current, name);
	control->current = control->current->next;
	notifyChange(MPCOMM_TITLES);
	if (endsWith(stream, ".m3u") || endsWith(stream, ".pls")) {
		addMessage(1, "Remote playlist..");
		control->list = 1;
	}
	else {
		control->list = 0;
	}

	/* no strdup() as this will be recycled on each setStream() call */
	control->streamURL =
		(char *) frealloc(control->streamURL, strlen(stream) + 1);
	strcpy(control->streamURL, stream);
	unlockPlaylist();
	addMessage(1, "Play Stream %s (%s)", name, stream);
}

/**
 * sends a command to the player
 * also makes sure that commands are not overwritten
 */
void setCommand(mpcmd_t cmd, char *arg) {
	mpconfig_t *config = getConfig();

	if ((cmd == mpc_idle) ||
		(config->status == mpc_quit) || (config->status == mpc_reset)) {
		return;
	}

	if (cmd == mpc_reset) {
		config->watchdog = WATCHDOG_TIMEOUT;
		config->command = cmd;
		return;
	}

	if (pthread_mutex_trylock(&_pcmdlock) == EBUSY) {
		/* Wait until someone unlocks */
		addMessage(1, "%s waiting to be set", mpcString(cmd));
		pthread_mutex_lock(&_pcmdlock);
	}

	/* wait for the last command to be handled */
	while (config->command != mpc_idle) {
		addMessage(1, "%s blocked on %s/%s", mpcString(cmd),
				   mpcString(config->command), mpcString(config->status));
		pthread_cond_wait(&_pcmdcond, &_pcmdlock);
		addMessage(1, "unblocked on %s/%s", mpcString(config->command),
				   mpcString(config->status));
	}

	/* player is being reset or about to quit - do not handle any commands */
	if ((config->status != mpc_reset) && (config->status != mpc_quit)) {
		addMessage(1, "Setting %s..", mpcString(cmd));
		/* someone did not clean up! */
		assert(config->argument == NULL);
		config->command = cmd;
		config->argument = arg;
	}
	else {
		addMessage(-1, "%s dropped for %s!", mpcString(cmd),
				   mpcString(config->status));
	}
	pthread_mutex_unlock(&_pcmdlock);
}

static int oactive = 1;

static int toPlayer(int fd, const char *msg) {
	return dowrite(fd, msg, strlen(msg));
}

/**
 * make mpeg123 play the given title
 */
static void sendplay(int fdset) {
	char line[MAXPATHLEN + 13] = "load ";
	mpconfig_t *control = getConfig();

	assert(control->current != NULL);

	if (control->mpmode & PM_STREAM) {
		if (control->status == mpc_play) {
			addMessage(-1, "Not loading stream on active player!");
			return;
		}
		if (control->list) {
			strcpy(line, "loadlist 1 ");
		}
		strtcat(line, control->streamURL, MAXPATHLEN + 6);
	}
	else {
		strtcat(line, fullpath(control->current->title->path), MAXPATHLEN + 6);
	}
	strtcat(line, "\n", MAXPATHLEN + 6);
	if (toPlayer(fdset, line) == -1) {
		fail(errno, "Could not write\n%s", line);
	}
	notifyChange(MPCOMM_TITLES);
	addMessage(2, "CMD: %s", line);
}

static void startPlayer() {
	mpconfig_t *control = getConfig();

	if (control->status != mpc_idle) {
		setCommand(mpc_stop, NULL);
	}
	setCommand(mpc_start, NULL);
}

/**
 * sets the current profile
 * This is thread-able to have progress information on startup!
 */
void *setProfile(void *arg) {
	profile_t *profile;
	int num;
	int64_t active;
	static int lastact = 0;		/* last active profile (not channel!) */
	mpconfig_t *control = getConfig();
	char *home = getenv("HOME");

	if (home == NULL) {
		fail(-1, "Cannot get homedir!");
	}

	blockSigint();
	activity(0, "Changing profile");
	addMessage(2, "New Thread: setProfile(%d)", control->active);

	if (control->active == 0) {
		control->active = oactive;
	}

	active = control->active;
	control->searchDNP = 0;

	/* stream selected */
	if (active < 0) {
		active = -(active + 1);
		profile = control->stream[active];

		if (active >= control->streams) {
			addMessage(0, "Stream #%" PRId64 " does no exist!", active);
			control->active = 1;
			return setProfile(NULL);
		}

		setStream(profile->stream, profile->name);
	}
	/* profile selected */
	else if (active > 0) {
		active = active - 1;
		addMessage(1, "Playmode=%u", control->mpmode);

		if (active > control->profiles) {
			addMessage(0, "Profile #%" PRId64 " does no exist!", active);
			control->active = 1;
			return setProfile(NULL);
		}

		/* only load database if it has not yet been used */
		if (control->root == NULL) {
			control->root = dbGetMusic();
			if (NULL == control->root) {
				addMessage(0, "Scanning musicdir");
				num = dbAddTitles(control->musicdir);
				if (0 == num) {
					fail(F_FAIL, "No music found at %s!", control->musicdir);
				}
				addMessage(0, "Added %i titles.", num);
				control->root = dbGetMusic();
				if (NULL == control->root) {
					fail(F_FAIL,
						 "No music found at %s for database %s!\nThis should never happen!",
						 control->musicdir, control->dbname);
				}
			}
		}

		/* change mode */
		profile = control->profile[active];
		/* last database play was on the same profile, so just wipe playlist */
		if (lastact == control->active) {
			control->current =
				wipePlaylist(control->current, control->mpmode & PM_STREAM);
			control->mpmode = PM_DATABASE | PM_SWITCH;
		}
		/* last database play was a different profile, so clean up all */
		else {
			cleanTitles(1);
			control->mpmode = PM_DATABASE | PM_SWITCH;
			control->dnplist = wipeList(control->dnplist);
			control->favlist = wipeList(control->favlist);
			control->dnplist = loadList(mpc_dnp);
			control->favlist = loadList(mpc_fav);
			if (control->dbllist == NULL) {
				control->dbllist = loadList(mpc_doublets);
				applyDNPlist(control->dbllist, 1);
			}
			applyLists(1);
			lastact = control->active;
		}
		plCheck(0);

		addMessage(1, "Profile set to %s.", profile->name);
		if (control->argument != NULL) {
			/* do not free, the string has become the new profile entry! */
			control->argument = NULL;
		}
	}
	else {
		addMessage(-1, "No valid profile selected!");
		addMessage(1, "Restart play");
		plCheck(0);
		startPlayer();
		return arg;
	}

	if (profile->volume == -1) {
		profile->volume = control->volume;
	}
	else {
		setVolume(profile->volume);
	}
	notifyChange(MPCOMM_CONFIG);

	sleep(1);
	startPlayer();

	/* make sure that progress messages are removed */
	notifyChange(MPCOMM_TITLES);
	addMessage(2, "End Thread: setProfile()");
	return arg;
}

/**
 * asnchronous functions to run in the background and allow updates being sent to the
 * client
 */
static void *plCheckDoublets(void *arg) {
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;
	int i;

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
	int i;

	addMessage(0, "Database Cleanup");

	/* update database with current playcount etc */
	dbWrite(0);

	addMessage(0, "Checking for deleted titles..");
	i = dbCheckExist();

	if (i > 0) {
		addMessage(0, "Removed %i titles", i);
		dbWrite(1);
		plCheck(1);
	}
	else {
		addMessage(0, "No titles removed");
	}

	addMessage(0, "Checking for new titles..");
	i = dbAddTitles(control->musicdir);

	if (i > 0) {
		addMessage(0, "Added %i new titles", i);
		dbWrite(1);
	}
	else {
		addMessage(0, "No titles to be added");
	}

	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

static void *plDbFix(void *arg) {
	mpconfig_t *control = getConfig();
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;

	addMessage(0, "Database smooth");
	dumpInfo(control->root, 1);
	unlockClient(-1);
	pthread_mutex_unlock(lock);
	return NULL;
}

static void *plDbInfo(void *arg) {
	mpconfig_t *control = getConfig();
	pthread_mutex_t *lock = (pthread_mutex_t *) arg;

	addMessage(0, "Database Info");
	dumpInfo(control->root, 0);
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
 * adds searchresults to the playlist
 * range - title-display/artist/album
 * arg - either a title key or a string
 * insert - play next or append to the end of the playlist
 */
static int playResults(mpcmd_t range, const char *arg, const int insert) {
	mpconfig_t *config = getConfig();
	mpplaylist_t *pos = config->current;
	mpplaylist_t *res = config->found->titles;
	mptitle_t *title = NULL;
	int key = atoi(arg);

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

static char *getProfileName(int profile) {
	mpconfig_t *config = getConfig();

	if (profile == 0) {
		return "NONE";
	}
	if (profile < 0) {
		profile = -(profile + 1);
		return config->stream[profile]->name;
	}
	else {
		profile = profile - 1;
		return config->profile[profile]->name;
	}
}

static void *killPlayers(pid_t pid[2], int p_command[2][2], int p_status[2][2],
						 int p_error[2][2], int restart) {
	uint64_t i;
	mpconfig_t *control = getConfig();
	unsigned players = (control->fade > 0) ? 2 : 1;

	if (restart) {
		addMessage(1, "kill and restart reader");
		control->watchdog = WATCHDOG_TIMEOUT;
		if (control->current == NULL) {
			addMessage(-1, "Restarting on empty player!");
		}

		/* if this is already a retry, fall back to something better */
		if (control->status == mpc_start) {
			if (oactive == control->active) {
				addMessage(-1, "Switching to default profile");
				control->active = 1;
			}
			else {
				addMessage(-1, "Switching back to %s",
						   getProfileName(oactive));
				control->active = oactive;
			}
		}

		/* Most likely an URL could not be loaded */
		if (control->active == 0) {
			addMessage(1, "Reverting to %s", getProfileName(oactive));
			control->active = oactive;
		}

		/* starting on an error? Not good.. */
		if ((control->status == mpc_start) && (control->mpmode & PM_DATABASE)) {
			addMessage(-1, "Music database failure!");
			control->status = mpc_quit;
			control->watchdog = 0;
		}
	}

	/* ask nicely first.. */
	for (i = 0; i < players; i++) {
		activity(0, "Stopping player %" PRId64, i);
		toPlayer(p_command[i][1], "QUIT\n");
		close(p_command[i][1]);
		close(p_status[i][0]);
		close(p_error[i][0]);
		sleep(1);
		if (waitpid(pid[i], NULL, WNOHANG | WCONTINUED) != pid[i]) {
			activity(1, "Terminating player %" PRId64, i);
			kill(pid[i], SIGTERM);
			sleep(1);
			if (waitpid(pid[i], NULL, WNOHANG | WCONTINUED) != pid[i]) {
				activity(1, "Killing player %" PRId64, i);
				kill(pid[i], SIGKILL);
				sleep(1);
				if (waitpid(pid[i], NULL, WNOHANG | WCONTINUED) != pid[i]) {
					addMessage(-1, "Could not get rid of %i!", pid[i]);
				}
			}
		}
	}
	activity(0, "Players stopped!");
	closeAudio();
	if (!asyncTest()) {
		addMessage(1, "Shutting down on active async!");
	}
	pthread_mutex_unlock(&_asynclock);
	control->command = mpc_idle;
	pthread_cond_signal(&_pcmdcond);
	activity(1, "All unlocked");
	return NULL;
}

static void stopPlay(int channel) {
	mpconfig_t *control = getConfig();

	if (control->status == mpc_stop) {
		addMessage(1, "Patience we are altready stopping..");
		return;
	}

	if (control->status != mpc_idle) {
		control->status = mpc_stop;
		toPlayer(channel, "STOP\n");
	}
}

/* stops the current title. That means send a stop to a stream and a pause
 * to a database title. */
static void pausePlay(int channel) {
	mpconfig_t *control = getConfig();

	if (control->status == mpc_play) {
		if (control->mpmode & PM_STREAM) {
			stopPlay(channel);
		}
		else {
			toPlayer(channel, "PAUSE\n");
		}
	}
}

/**
 * the main control thread function that acts as an interface between the
 * player processes and the UI. It checks the control->command value for
 * commands from the UI and the status messages from the players.
 *
 * The original plan was to keep this UI independent so it could be used
 * in mixplay, gmixplay and probably other GUI variants (ie: web)
 */
void *reader() {
	mpconfig_t *control = getConfig();
	mptitle_t *ctitle = NULL;	/* the title commands should use */
	fd_set fds;
	struct timeval to;
	struct timespec ts;
	int64_t i, key;
	int invol = 80;
	int outvol = 80;
	int fdset = 0;
	int profile;
	char line[MAXPATHLEN];
	char *a, *t;
	int order = 1;
	float intime = 0.0;
	float oldtime = 0.0;
	int fading = 1;
	int p_status[2][2];			/* status pipes to mpg123 */
	int p_command[2][2];		/* command pipes to mpg123 */
	int p_error[2][2];			/* error pipes to mpg123 */
	pid_t pid[2];
	mpcmd_t cmd = mpc_idle;
	unsigned update = 0;
	unsigned insert = 0;
	unsigned skipped = 0;

	blockSigint();

	addMessage(1, "Reader starting");

	if (control->fade == 0) {
		addMessage(1, "No crossfading");
		fading = 0;
	}

	ts.tv_nsec = 250000;
	ts.tv_sec = 0;

	/* start the needed mpg123 instances */
	/* start the player processes */
	/* these may wait in the background until */
	/* something needs to be played at all */
	for (i = 0; i <= fading; i++) {
		addMessage(2, "Starting player %" PRId64, i + 1);

		/* create communication pipes */
		if ((pipe(p_status[i]) != 0) ||
			(pipe(p_command[i]) != 0) || (pipe(p_error[i]) != 0)) {
			fail(errno, "Could not create pipes!");
		}
		pid[i] = fork();
		/* todo: consider spawn() instead
		 * https://unix.stackexchange.com/questions/252901/get-output-of-posix-spawn
		 */

		if (0 > pid[i]) {
			fail(errno, "could not fork");
		}

		/* child process */
		if (0 == pid[i]) {
			if (dup2(p_command[i][0], STDIN_FILENO) != STDIN_FILENO) {
				fail(errno, "Could not dup stdin for player %" PRId64, i + 1);
			}

			if (dup2(p_status[i][1], STDOUT_FILENO) != STDOUT_FILENO) {
				fail(errno, "Could not dup stdout for player %" PRId64, i + 1);
			}

			if (dup2(p_error[i][1], STDERR_FILENO) != STDERR_FILENO) {
				fail(errno, "Could not dup stderr for player %" PRId64, i + 1);
			}

			/* this process needs no pipe handles */
			close(p_command[i][0]);
			close(p_command[i][1]);
			close(p_status[i][0]);
			close(p_status[i][1]);
			close(p_error[i][0]);
			close(p_error[i][1]);
			/* Start mpg123 in Remote mode */
			execlp("mpg123", "mpg123", "-R", "--rva-mix", NULL);
			fail(errno, "Could not exec mpg123");
		}

		close(p_command[i][0]);
		close(p_status[i][1]);
		close(p_error[i][1]);
	}

	control->watchdog = 0;

	/* check if we can control the system's volume */
	control->volume = getVolume();
	switch (control->volume) {
	case -2:
		addMessage(1, "Hardware volume is muted");
		break;
	case -1:
		addMessage(0, "Hardware volume control is diabled");
		control->channel = NULL;
		break;
	default:
		addMessage(1, "Hardware volume level is %i%%", control->volume);
	}

	/* main loop */
	do {
		FD_ZERO(&fds);
		for (i = 0; i <= fading; i++) {
			FD_SET(p_status[i][0], &fds);
			FD_SET(p_error[i][0], &fds);
		}
		to.tv_sec = 1;
		to.tv_usec = 0;			/* 1/10 second */
		i = select(FD_SETSIZE, &fds, NULL, NULL, &to);
		update = 0;

		/**
		 * status is not idle but we did not get any updates from either player
		 * after ten times we decide all hope is lost and we take the hard way
		 * out.
		 *
		 * this may happen when a stream is played and the network connection
		 * changes - most likely due to a DSL reconnect - or if the stream host
		 * fails.
		 */
		if (control->watchdog >= WATCHDOG_TIMEOUT) {
			addMessage(1, "Player restart! cmd:%s - status:%s",
					   mpcString(control->command),
					   mpcString(control->status));
			return killPlayers(pid, p_command, p_status, p_error, 1);
		}
		if ((i == 0) && (control->mpmode & PM_STREAM) &&
			(control->status != mpc_idle)) {
			control->watchdog++;
		}
		else {
			control->watchdog = 0;
		}

		if (fading) {
			/* drain inactive player */
			if (FD_ISSET(p_status[fdset ? 0 : 1][0], &fds)) {
				key = readline(line, MAXPATHLEN, p_status[fdset ? 0 : 1][0]);
				if (key > 2) {
					if ('@' == line[0]) {
						if (('F' != line[1]) && ('V' != line[1])
							&& ('I' != line[1])) {
							addMessage(2, "P- %s", line);
						}

						switch (line[1]) {
						case 'R':	/* startup */
							addMessage(1, "MPG123 background instance is up");
							break;
						case 'E':
							if (control->current == NULL) {
								addMessage(-1, "BG: %s", line);
							}
							else {
								/*  *INDENT-OFF*  */
								addMessage(-1, "BG: %s\n> Index: %i\n> Name: %s\n> Path: %s",
										   line, control->current->title->key,
										   control->current->title->display,
										   fullpath(control->current->title->path));
								/*  *INDENT-ON*  */
							}
							return killPlayers(pid, p_command, p_status,
											   p_error, 1);
							break;
						case 'F':
							if (outvol > 0) {
								outvol--;
								snprintf(line, MAXPATHLEN, "volume %i\n",
										 outvol);
								toPlayer(p_command[fdset ? 0 : 1][1], line);
							}
							break;
						}
					}
					else {
						addMessage(1, "OFF123: %s", line);
					}
				}
			}
			/* this shouldn't be happening but if it happens, it gives a hint */
			if (FD_ISSET(p_error[fdset ? 0 : 1][0], &fds)) {
				key = readline(line, MAXPATHLEN, p_error[fdset ? 0 : 1][0]);
				if (key > 1) {
					addMessage(-1, "BE: %s", line);
				}
			}
		}

		/* Interpret mpg123 output and ignore invalid lines */
		if (FD_ISSET(p_status[fdset][0], &fds) &&
			(3 < readline(line, MAXPATHLEN, p_status[fdset][0]))) {
			if ('@' == line[0]) {
				/* Don't print volume, tag and progress messages */
				if (('F' != line[1]) && ('V' != line[1]) && ('I' != line[1])) {
					addMessage(2, "P+ %s", line);
				}
				/* Print tag info */
				if ('I' == line[1]) {
					addMessage(3, "P+ %s", line);
				}
				switch (line[1]) {
					int cmd;
					float rem;

				case 'R':		/* startup */
					addMessage(1, "MPG123 foreground instance is up");
					break;

				case 'I':		/* ID3 info */
					/* ignore ID3 info on database play */
					if (control->mpmode & PM_DATABASE) {
						break;
					}
					/* ICY stream info */
					if ((control->current != NULL)
						&& (NULL != strstr(line, "ICY-"))) {
						if (NULL != strstr(line, "ICY-NAME: ")) {
							if (control->current->prev != NULL) {
								strip(control->current->prev->title->title,
									  line + 13, NAMELEN - 1);
								update = 1;
							}
						}

						if (NULL != (a = strstr(line, "StreamTitle"))) {
							addMessage(3, "%s", a);
							a = a + 13;
							t = strchr(a, ';');
							if (t != NULL) {
								t--;
								*t = 0;
							}
							/* only do this if the title actually changed */
							if (strcmp(a, control->current->title->display)) {
								if (strlen(control->current->title->album) > 0) {
									control->current =
										addPLDummy(control->current, a);
								}
								strip(control->current->title->display, a,
									  MAXPATHLEN - 1);
								strip(control->current->title->title, a,
									  NAMELEN - 1);
								/* if possible cut up title and artist */
								if (NULL != (t = strstr(a, " - "))) {
									*t = 0;
									t = t + 3;
									strip(control->current->title->artist, a,
										  NAMELEN - 1);
									strip(control->current->title->title, t,
										  NAMELEN - 1);
								}
								plCheck(0);
								/* carry over stream title as album entry */
								strcpy(control->current->title->album,
									   control->current->prev->title->title);
								update = 1;
							}
						}
					}
					break;

				case 'T':		/* TAG reply */
					addMessage(1, "Got TAG reply?!");
					break;

				case 'J':		/* JUMP reply */
					break;

				case 'S':		/* Status message after loading a song (stream info) */
					break;

				case 'F':		/* Status message during playing (frame info) */
					/* $1   = framecount (int)
					 * $2   = frames left this song (int)
					 * in  = seconds (float)
					 * rem = seconds left (float)
					 */
					a = strrchr(line, ' ');
					if (a == NULL) {
						addMessage(0, "Error in Frame info: %s", line);
						break;
					}
					rem = strtof(a, NULL);
					*a = 0;
					a = strrchr(line, ' ');
					if (a == NULL) {
						addMessage(0, "Error in Frame info: %s", line);
						break;
					}
					intime = strtof(a, NULL);

					if (invol < 100) {
						invol++;
						snprintf(line, MAXPATHLEN, "volume %i\n", invol);
						toPlayer(p_command[fdset][1], line);
					}

					if (intime != oldtime) {
						oldtime = intime;
						update = 1;
					}
					else {
						break;
					}

					control->playtime = (unsigned) roundf(intime);
					/* file play */
					if (!(control->mpmode & PM_STREAM)) {
						control->percent = (100 * intime) / (rem + intime);
						control->remtime = (unsigned) roundf(rem);

						/* we could just be switching from playlist to database */
						if (control->current == NULL) {
							if (!(control->mpmode & PM_SWITCH)) {
								addMessage(1,
										   "No current title and not switching!");
							}
							break;
						}

						/* Is the player initializing or changing profiles */
						if (playerIsBusy()) {
							break;
						}

						/* this is bad! */
						if (control->current->title == NULL) {
							addMessage(0, "No current title!");
							break;
						}

						if ((fading) && (rem <= control->fade)) {
							/* should the playcount be increased? */
							playCount(control->current->title, skipped);
							skipped = 0;

							if (control->current->next == control->current) {
								control->status = mpc_idle;	/* Single song: STOP */
							}
							else {
								control->current = control->current->next;
								plCheck(0);
								/* swap players */
								fdset = fdset ? 0 : 1;
								invol = 0;
								outvol = 100;
								toPlayer(p_command[fdset][1], "volume 0\n");
								sendplay(p_command[fdset][1]);
							}
						}
					}
					break;

				case 'P':		/* Player status */
					cmd = atoi(&line[3]);

					switch (cmd) {
					case 0:	/* STOP */
						addMessage(1, "FG Player stopped");
						/* player was not yet fully initialized or the current
						 * profile/stream has changed start again */
						if (control->status == mpc_stop) {
							control->status = mpc_idle;
							break;
						}
						/* unclear how this happens... */
						if (control->status == mpc_start) {
							break;
						}
						/* stream stopped playing (PAUSE/switch profile) */
						else if (control->mpmode & PM_STREAM) {
							addMessage(1, "Stream stopped");
							/* stream stopped without user interaction */
							if (control->status == mpc_play) {
								addMessage(1, "Trying to restart stream");
								control->status = mpc_start;
								sendplay(p_command[fdset][1]);
							}
							else {
								control->status = mpc_idle;
							}
						}
						/* we're playing a playlist */
						else if ((control->current != NULL) &&
								 !(control->mpmode & PM_SWITCH)) {
							addMessage(2, "Title change");
							if (order == 1) {
								if (playCount(control->current->title, skipped)
									== 1) {
									order = 0;
								}
								skipped = 0;
							}

							if (order < 0) {
								while ((control->current->prev != NULL)
									   && order < 0) {
									control->current = control->current->prev;
									order++;
								}
								/* ignore skip before first title in playlist */
							}

							if (order > 0) {
								while ((control->current->next != NULL)
									   && order > 0) {
									control->current = control->current->next;
									order--;
								}
								/* stop on end of playlist */
								if (order > 0) {
									control->status = mpc_idle;	/* stop */
								}
							}

							if (control->status != mpc_idle) {
								sendplay(p_command[fdset][1]);
							}

							if (control->mpmode == PM_DATABASE) {
								plCheck(0);
							}
						}
						/* always re-enable proper playorder after stop */
						order = 1;
						break;

					case 1:	/* PAUSE */
						control->status = mpc_pause;
						break;

					case 2:	/* PLAY */
						if (control->mpmode & PM_SWITCH) {
							addMessage(1, "Playing profile #%i: %s",
									   control->active,
									   getProfileName(control->active));
							control->mpmode &= ~PM_SWITCH;
							if (control->active != 0) {
								writeConfig(NULL);
								oactive = control->active;
							}
						}
						control->status = mpc_play;
						break;

					default:
						addMessage(0, "Unknown status %i on FG player!\n%s",
								   cmd, line);
						break;
					}

					update = 1;
					break;

				case 'V':		/* volume reply */
					break;

				case 'E':
					addMessage(-1, "FG: %s!", line + 3);
					if (control->current != NULL) {
						if (control->mpmode & PM_STREAM) {
							addMessage(1, "FG: %s <- %s\n Name: %s\n URL: %s",
									   getProfileName(control->active),
									   getProfileName(oactive),
									   control->current->title->display,
									   control->streamURL);
						}
						else {
							/*  *INDENT-OFF*  */
							addMessage(1, "FG: %i\n> Name: %s\n> Path: %s",
									   control->current->title->key,
									   control->current->title->display,
									   fullpath(control->current->title->path));
							/*  *INDENT-ON*  */
						}
					}
					return killPlayers(pid, p_command, p_status, p_error, 1);
					break;

				default:
					addMessage(0, "Warning: %s", line);
					break;
				}				/* case line[1] */
			}					/* if line starts with '@' */
			else {
				/* verbosity 1 as sometimes tags appear here which confuses on level 0 */
				addMessage(1, "Raw: %s", line);
			}
		}						/* FD_ISSET( p_status ) */

		if (FD_ISSET(p_error[fdset][0], &fds)) {
			key = readline(line, MAXPATHLEN, p_error[fdset][0]);
			if (key > 1) {
				if (strstr(line, "rror: ")) {
					addMessage(-1, "%s", strstr(line, "rror: ") + 6);
					return killPlayers(pid, p_command, p_status, p_error, 1);
				}
				else if (strstr(line, "Warning: ") == line) {
					/* ignore content-type warnings */
					if (!strstr(line, "content-type")) {
						addMessage(0, "FE: %s", line);
					}
				}
				else {
					addMessage(1, "FE: %s", line);
				}
			}
		}

		cmd = MPC_CMD(control->command);

		/* get the target title for fav and dnp commands */
		if (control->current != NULL) {
			ctitle = control->current->title;
			if ((cmd == mpc_fav) || (cmd == mpc_dnp)) {
				if (control->argument != NULL) {
					/* todo: check if control->argument is a number */
					if (MPC_EQDISPLAY(control->command)) {
						ctitle = getTitleByIndex(atoi(control->argument));
					}
					else {
						ctitle =
							getTitleForRange(control->command,
											 control->argument);
					}
					/* someone is testing parameters, mh? */
					if (ctitle == NULL) {
						addMessage(0, "Nothing matches %s!",
								   control->argument);
					}
					sfree(&(control->argument));
				}
			}
		}

		switch (cmd) {
		case mpc_start:
			addMessage(1, "mpc_start (%s)", mpcString(control->status));
			if (control->status == mpc_start) {
				addMessage(1, "Already starting!");
			}
			else {
				plCheck(0);
				control->status = mpc_start;
				sendplay(p_command[fdset][1]);
				notifyChange(MPCOMM_CONFIG);
			}
			break;

		case mpc_play:
			addMessage(1, "mpc_play (%s)", mpcString(control->status));
			/* has the player been properly initialized yet? */
			if (control->status != mpc_start) {
				/* simple stop */
				if (control->status == mpc_play) {
					pausePlay(p_command[fdset][1]);
				}
				/* play */
				else {
					/* play stream */
					if (control->mpmode & PM_STREAM) {
						sendplay(p_command[fdset][1]);
					}
					/* unpause on normal play */
					else {
						toPlayer(p_command[fdset][1], "PAUSE\n");
					}
				}
			}
			/* initialize player after startup */
			/* todo: what happens if the user sends a play during startup? */
			else {
				plCheck(0);
				if (control->current != NULL) {
					addMessage(1, "Autoplay..");
					sendplay(p_command[fdset][1]);
				}
			}
			break;

		case mpc_prev:
			/* This /may/ make sense on streamlists but so far a stream has no
			 * previous or next title */
			if (control->mpmode & PM_STREAM)
				break;
			if (control->playtime > 3) {
				toPlayer(p_command[fdset][1], "JUMP 0\n");
				control->percent = 0;
				control->playtime = 0;
			}
			else if (asyncTest()) {
				order = -1;
				if (control->argument != NULL) {
					order = -atoi(control->argument);
				}
				toPlayer(p_command[fdset][1], "STOP\n");
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_next:
			/* This /may/ make sense on streamlists but so far a stream has no
			 * previous or next title */
			if (control->mpmode & PM_STREAM)
				break;
			if ((control->current != NULL) && asyncTest()) {
				order = 1;
				if (control->argument != NULL) {
					order = atoi(control->argument);
				}
				else {
					skipped = 1;
				}
				toPlayer(p_command[fdset][1], "STOP\n");
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_doublets:
			if (control->mpmode & PM_STREAM)
				break;
			if (checkPasswd()) {
				asyncRun(plCheckDoublets);
			}
			break;

		case mpc_dbclean:
			if (control->mpmode & PM_STREAM)
				break;
			if (checkPasswd()) {
				asyncRun(plDbClean);
			}
			break;

		case mpc_stop:
			addMessage(1, "mpc_stop (%s)", mpcString(control->status));
			stopPlay(p_command[fdset][1]);
			break;

		case mpc_dnp:
			if (control->mpmode & PM_STREAM)
				break;
			if ((ctitle != NULL) && asyncTest()) {
				if (ctitle == control->current->title) {
					/* current title is affected, play the next one */
					order = 0;
					toPlayer(p_command[fdset][1], "STOP\n");
				}
				handleRangeCmd(ctitle, control->command);
				plCheck(1);
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_fav:
			if (control->mpmode & PM_STREAM)
				break;
			if ((ctitle != NULL) && asyncTest()) {
				handleRangeCmd(ctitle, control->command);
				notifyChange(MPCOMM_TITLES);
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_repl:
			if (control->mpmode & PM_STREAM)
				break;
			toPlayer(p_command[fdset][1], "JUMP 0\n");
			control->percent = 0;
			control->playtime = 0;
			break;

		case mpc_quit:
			/* The player does not know about the main App so anything setting
			 * mcp_quit MUST make sure that the main app terminates as well ! */
			if (checkPasswd()) {
				control->status = mpc_quit;
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_profile:
			if (asyncTest()) {
				if (control->argument == NULL) {
					addMessage(-1, "No profile given!");
				}
				else {
					profile = atoi(control->argument);
					if ((profile != 0) && (profile != control->active)) {
						if (!(control->mpmode & (PM_STREAM | PM_DATABASE))) {
							wipeTitles(control->root);
						}
						/* switching channels started */
						control->mpmode |= PM_SWITCH;
						/* write database if needed */
						dbWrite(0);
						if (control->active < 0) {
							control->stream[(-control->active) - 1]->volume =
								control->volume;
						}
						else if (control->active > 0) {
							control->profile[control->active - 1]->volume =
								control->volume;
						}
						control->active = profile;
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
			if ((control->current != NULL) && asyncTest()) {
				if (control->argument == NULL) {
					addMessage(-1, "No profile given!");
				}
				/* save the current stream */
				else if (control->active == 0) {
					control->streams++;
					control->stream =
						(profile_t **) frealloc(control->stream,
												control->streams *
												sizeof (profile_t *));
					control->stream[control->streams - 1] =
						createProfile(control->argument, control->streamURL, 0,
									  control->volume);
					control->active = -(control->streams);
				}
				/* just add argument as new profile */
				else {
					control->profiles++;
					control->profile =
						(profile_t **) frealloc(control->profile,
												control->profiles *
												sizeof (profile_t *));
					control->profile[control->profiles - 1] =
						createProfile(control->argument, NULL, 0,
									  control->volume);
				}
				writeConfig(NULL);
				control->argument = NULL;
				pthread_mutex_unlock(&_asynclock);
				notifyChange(MPCOMM_CONFIG);
			}
			break;

		case mpc_clone:
			if ((control->current != NULL) && asyncTest()) {
				if (control->argument == NULL) {
					addMessage(-1, "No profile given!");
				}
				/* only clone profiles */
				else if (control->active > 0) {
					control->profiles++;
					control->profile =
						(profile_t **) frealloc(control->profile,
												control->profiles *
												sizeof (profile_t *));
					control->profile[control->profiles - 1] =
						createProfile(control->argument, NULL, 0,
									  control->volume);
					control->active = control->profiles;
					writeList(mpc_fav);
					writeList(mpc_dnp);

					writeConfig(NULL);
					control->argument = NULL;
					notifyChange(MPCOMM_CONFIG);
				}
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_remprof:
			if (control->argument == NULL) {
				addMessage(-1, "No profile given!");
			}
			else {
				if (asyncTest()) {
					profile = atoi(control->argument);
					if (profile > 0) {
						if (profile == 1) {
							addMessage(-1, "mixplay cannot be removed.");
						}
						else if (profile == control->active) {
							addMessage(-1, "Cannot remove active profile!");
						}
						else if (profile > control->profiles) {
							addMessage(-1, "Profile #%i does not exist!",
									   profile);
						}
						else {
							free(control->profile[profile - 1]);
							for (i = profile; i < control->profiles; i++) {
								control->profile[i - 1] = control->profile[i];
								if (i == control->active) {
									control->active = control->active - 1;
								}
							}
							control->profiles--;
							writeConfig(NULL);
						}
					}
					else if (profile < 0) {
						profile = (-profile);
						if (profile > control->streams) {
							addMessage(-1, "Stream #%i does not exist!",
									   profile);
						}
						else if (-profile == control->active) {
							addMessage(-1, "Cannot remove active profile!");
						}
						else {
							freeProfile(control->stream[profile - 1]);
							for (i = profile; i < control->streams; i++) {
								control->stream[i - 1] = control->stream[i];
								if (i == control->active) {
									control->active = control->active - 1;
								}
							}
							control->streams--;
							control->stream =
								(profile_t **) frealloc(control->stream,
														control->streams *
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
			if ((control->current != NULL) && asyncTest()) {
				if (control->argument == NULL) {
					addMessage(-1, "No path given!");
				}
				else {
					if (setArgument(control->argument)) {
						control->active = 0;
						stopPlay(p_command[fdset][1]);
						sendplay(p_command[fdset][1]);
					}
				}
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_ivol:
			adjustVolume(+VOLSTEP);
			update = 1;
			break;

		case mpc_dvol:
			adjustVolume(-VOLSTEP);
			update = 1;
			break;

		case mpc_bskip:
			if (control->mpmode & PM_STREAM)
				break;
			toPlayer(p_command[fdset][1], "JUMP -64\n");
			break;

		case mpc_fskip:
			if (control->mpmode & PM_STREAM)
				break;
			toPlayer(p_command[fdset][1], "JUMP +64\n");
			break;

		case mpc_dbinfo:
			if (control->mpmode & PM_STREAM)
				break;
			if ((control->argument) && checkPasswd()) {
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
			if (control->mpmode & PM_STREAM)
				break;
			if (asyncTest()) {
				/* if we mix two searches we're in trouble! */
				assert(control->found->state != mpsearch_idle);
				if (control->argument == NULL) {
					if (MPC_ISARTIST(control->command)) {
						control->argument = strdup(ctitle->artist);
					}
					else {
						control->argument = strdup(ctitle->album);
					}
				}
				if (search(control->argument, MPC_MODE(control->command)) ==
					-1) {
					addMessage(0, "Too many titles found!");
				}
				/* todo: a signal/unblock would be nicer here */
				addMessage(1, "Waiting to send results..");
				while (control->found->state != mpsearch_idle) {
					nanosleep(&ts, NULL);
				}
				addMessage(1, "Results sent!");
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_insert:
			if (control->mpmode & PM_STREAM)
				break;
			insert = 1;
			/* fallthrough */

		case mpc_append:
			if (control->mpmode & PM_STREAM)
				break;
			if (control->argument != NULL) {
				playResults(MPC_RANGE(control->command), control->argument,
							insert);
			}
			insert = 0;
			break;

		case mpc_setvol:
			if (control->argument != NULL) {
				setVolume(atoi(control->argument));
			}
			update = 1;
			break;

		case mpc_smode:
			if (control->mpmode & PM_STREAM)
				break;
			control->searchDNP = ~(control->searchDNP);
			break;

		case mpc_deldnp:
		case mpc_delfav:
			if (control->mpmode & PM_STREAM)
				break;
			if (control->argument != NULL) {
				delFromList(cmd, control->argument);
			}
			break;

		case mpc_remove:
			if (control->mpmode & PM_STREAM)
				break;
			if (control->argument != NULL) {
				control->current =
					remFromPLByKey(control->current, atoi(control->argument));
				plCheck(0);
			}
			break;

		case mpc_mute:
			toggleMute();
			break;

		case mpc_favplay:
			if (control->mpmode & PM_STREAM)
				break;
			if (asyncTest()) {
				if (countTitles(MP_FAV, 0) < 21) {
					addMessage(-1,
							   "Need at least 21 Favourites to enable Favplay.");
					break;
				}
				if (toggleFavplay()) {
					addMessage(1, "Enabling Favplay");
				}
				else {
					addMessage(1, "Disabling Favplay");
				}
				cleanTitles(0);
				writeConfig(NULL);
				plCheck(0);
				sendplay(p_command[fdset][1]);
				pthread_mutex_unlock(&_asynclock);
			}
			break;

		case mpc_move:
			if (control->mpmode & PM_STREAM)
				break;
			if (control->argument != NULL) {
				t = strchr(control->argument, '/');
				if (t != NULL) {
					*t = 0;
					t++;
					moveTitleByIndex(atoi(control->argument), atoi(t));
				}
				else {
					moveTitleByIndex(atoi(control->argument), 0);
				}
				notifyChange(MPCOMM_TITLES);
			}
			break;

		case mpc_idle:
			/* read current Hardware volume in case it changed externally
			 * don't read before control->argument is NULL as someone may be
			 * trying to set the volume right now */
			if ((control->argument == NULL) && (control->volume != -1)) {
				control->volume = getVolume();
			}
			break;

		case mpc_reset:
			addMessage(-1, "Force restart!");
			control->status = mpc_reset;
			return killPlayers(pid, p_command, p_status, p_error, 1);

		default:
			addMessage(0, "Received illegal command %i", cmd);
			break;
		}

		sfree(&(control->argument));
		control->command = mpc_idle;
		pthread_cond_signal(&_pcmdcond);

		/* notify UI that something has changed */
		if (update) {
			updateUI();
		}
	}
	while (control->status != mpc_quit);

	dbWrite(0);

	/* stop player(s) gracefully */
	return killPlayers(pid, p_command, p_status, p_error, 0);
}
