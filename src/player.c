/*
 * player.c
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
 */

#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <sys/wait.h>
#include <poll.h>

#include "mpalsa.h"
#include "database.h"
#include "controller.h"

#define MPV 10
#define WATCHDOG_TIMEOUT 15

static pthread_mutex_t _killlock = PTHREAD_MUTEX_INITIALIZER;

static int32_t fdset = 0;		/* the currently active player */
static int32_t p_command[2][2];	/* command pipes to mpg123 */
static int32_t p_status[2][2];	/* status pipes to mpg123 */
static int32_t p_error[2][2];	/* error pipes to mpg123 */
static pid_t p_pid[2];			/* player pids */
static int32_t p_order = 1;		/* playing order */
static int32_t p_skipped = 0;	/* playing order */

void setSkipped() {
	p_skipped = 1;
}

/*
 * todo: does this need to be made thread safe?
 */
void setOrder(int32_t order) {
	p_order = order;
}

/* clean up when switching mode
 * if flags is true, all title flags will be cleared, only
 * MP_DBL will be kept.
 */
void cleanTitles(bool flags) {
	mpconfig_t *control = getConfig();
	mptitle_t *runner = control->root;
	uint32_t pc = getPlaycount(true);

	if (pc > 0) {
		pc--;
	}

	wipePlaylist(control);

	/* this should even be assert() worthy */
	if (runner == NULL) {
		addMessage(-1, "Switching Favplay without active database!");
		return;
	}
	do {
		runner->favpcount = getFavplay()? 0 : MIN(runner->playcount, pc);
		runner = runner->next;
		if (flags) {
			/* just keep MP_DBL */
			runner->flags &= MP_DBL;
		}
		else {
			/* new list, nothing should be matching */
			runner->flags &= ~MP_TDARK;
		}
	} while (runner != control->root);
}

/**
 * sets the given stream
 */
void setStream(char const *const stream, char const *const name) {
	mpconfig_t *control = getConfig();

	wipePlaylist(control);
	control->mpmode = PM_STREAM | PM_SWITCH;
	control->current = addPLDummy(control->current, "<waiting for info>");
	control->current = addPLDummy(control->current, name);
	control->current = control->current->next;
	notifyChange(MPCOMM_TITLES);
	if (endsWith(stream, ".m3u") || endsWith(stream, ".pls")) {
		addMessage(MPV + 1, "Remote playlist..");
		control->list = 1;
	}
	else {
		control->list = 0;
	}

	/* no strdup() as this will be recycled on each setStream() call */
	control->streamURL =
		(char *) frealloc(control->streamURL, strlen(stream) + 1);
	strcpy(control->streamURL, stream);
	addMessage(MPV + 1, "Play Stream %s (%s)", name, stream);
}

static uint32_t oactive = 1;

/*
 * send something to a player
 * player: 0 - currently active player
 *         1 - inactive player
 */
int32_t toPlayer(int32_t player, const char *msg) {
	return dowrite(p_command[player ? (fdset ? 0 : 1) : fdset][1], msg,
				   strlen(msg));
}

/**
 * make mpeg123 play the given title
 */
void sendplay(void) {
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
	if (toPlayer(0, line) == -1) {
		fail(errno, "Could not write\n%s", line);
	}
	notifyChange(MPCOMM_TITLES);
	addMessage(MPV + 2, "CMD: %s", line);
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
	int32_t num;
	uint32_t active;
	static uint32_t lastact = 0;	/* last active profile (not channel!) */
	mpconfig_t *control = getConfig();
	char *home = getenv("HOME");

	if (home == NULL) {
		fail(-1, "Cannot get homedir!");
	}

	blockSigint();
	activity(0, "Setting profile");
	addMessage(MPV + 2, "New Thread: setProfile(%" PRId32 ")",
			   control->active);

	if (control->active == 0) {
		control->active = oactive;
	}

	active = control->active;
	control->searchDNP = 0;

	profile = getProfile(active);
	if (profile == NULL) {
		addMessage(0, "No profile with ID %" PRIu32 "!", active);
		if (control->active == oactive)
			control->active = 1;
		else
			control->active = oactive;
		/* todo: get rid of this recursion! */
		return setProfile(NULL);
	}

	/* stream selected */
	if (isStream(profile)) {
		setStream(profile->url, profile->name);
	}
	/* profile selected */
	else {
		addMessage(MPV + 1, "Playmode=%u", control->mpmode);

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

		/* last database play was on the same profile, so just wipe playlist */
		if (lastact == control->active) {
			wipePlaylist(control);
			control->mpmode = PM_DATABASE | PM_SWITCH;
		}
		/* last database play was a different profile, so clean up all */
		else {
			cleanTitles(true);
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
		setArtistSpread();
		plCheck(true);
		addMessage(MPV + 1, "Profile set to %s.", profile->name);
	}

	notifyChange(MPCOMM_CONFIG);

//  usleep(500);                // why?
	setVolume(profile->volume);
	startPlayer();

	/* make sure that progress messages are removed */
	notifyChange(MPCOMM_TITLES);
	addMessage(MPV + 2, "End Thread: setProfile()");
	return arg;
}

/* returns the name of the current profile or channel */
static const char *getProfileName(int32_t profile) {
	if (profile == 0) {
		return "NONE";
	}
	return getProfile(profile)->name;
}

/* kills (and restarts) the player loop and decoders
 */
void *killPlayers(int32_t restart) {
	mpconfig_t *control = getConfig();
	uint32_t players = (control->fade > 0) ? 2 : 1;

	if (pthread_mutex_trylock(&_killlock) == EBUSY) {
		addMessage(0, "Player is already in %s!", mpcString(control->status));
		return NULL;
	}

	if (restart) {
		activity(0, "Restarting players");
		if (control->current == NULL) {
			addMessage(-1, "Restarting on empty player!");
		}

		/* if this is already a retry, fall back to something better */
		if (control->status == mpc_start) {
			/* Most likely an URL could not be loaded */
			if (oactive != control->active) {
				addMessage(MPV + 1, "Reverting to %s",
						   getProfileName(oactive));
				control->active = oactive;
			}
			else {
				addMessage(0, "Switching to default profile");
				control->active = 1;
			}
		}

		/* starting on an error? Not good.. */
		if ((control->status == mpc_start) && (control->mpmode & PM_DATABASE)) {
			addMessage(-1, "Music database failure!");
			control->status = mpc_quit;
		}
		else {
			control->status = mpc_reset;
		}
	}
	else {
		control->status = mpc_quit;
		activity(0, "Stopping players");
	}

	/* ask nicely first.. */
	for (unsigned i = 0; i < players; i++) {
		addMessage(2, "Stopping player %u", i);
		if (toPlayer(i, "QUIT\n") != -1) {
			sleep(1);			// if the player is still listening, give it a chance to react
		}
		close(p_command[i][1]);
		close(p_status[i][0]);
		close(p_error[i][0]);
		/* still there? */
		if (waitpid(p_pid[i], NULL, WNOHANG | WCONTINUED) != p_pid[i]) {
			addMessage(1, "Terminating player %u", i);
			kill(p_pid[i], SIGTERM);
			sleep(1);			// Let the SIGTERM soak
			/* STILL there?? */
			if (waitpid(p_pid[i], NULL, WNOHANG | WCONTINUED) != p_pid[i]) {
				addMessage(0, "Killing player %u", i);
				kill(p_pid[i], SIGKILL);
				sleep(1);		// Let the SIGKILL soak
				/* This is bad... */
				if (waitpid(p_pid[i], NULL, WNOHANG | WCONTINUED) != p_pid[i]) {
					addMessage(-1, "Could not get rid of %i!", p_pid[i]);
				}
			}
		}
	}

	addMessage(MPV + 1, "Players stopped!");
	closeAudio();
	unlockController();
	activity(0, "All unlocked");
	pthread_mutex_unlock(&_killlock);
	return NULL;
}

void stopPlay(void) {
	mpconfig_t *control = getConfig();

	if (control->status == mpc_stop) {
		addMessage(MPV + 1, "Patience, we are already stopping..");
		return;
	}

	if (control->status != mpc_idle) {
		control->status = mpc_stop;
		toPlayer(0, "STOP\n");
	}
}

/* stops the current title. That means send a stop to a stream and a pause
 * to a database title. */
void pausePlay(void) {
	mpconfig_t *control = getConfig();

	if (control->status == mpc_play) {
		if (control->mpmode & PM_STREAM) {
			stopPlay();
		}
		else {
			toPlayer(0, "PAUSE\n");
		}
	}
}

/**
 * the main player loop.
 * This starts and listens to the mpg123 processes and acts accordingly on
 * information and status changes. It also controls volume fading when
 * available and checks for general health of the decoders.
 */
void *reader( __attribute__ ((unused))
			 void *arg) {
	mpconfig_t *control = getConfig();
	struct pollfd pfd[4];
	int64_t key;
	int32_t invol = 80;
	int32_t outvol = 80;
	char line[MAXPATHLEN];
	float intime = 0.0;
	float oldtime = 0.0;
	int32_t fading = 1;
	uint32_t watchdog = 0;

	blockSigint();

	addMessage(MPV + 1, "Reader starting");

	if (control->fade == 0) {
		addMessage(MPV + 1, "No crossfading");
		fading = 0;
	}

	/* start the needed mpg123 instances */
	/* start the player processes */
	/* these may wait in the background until */
	/* something needs to be played at all */
	for (int i = 0; i <= fading; i++) {
		addMessage(MPV + 2, "Starting player %i", i + 1);

		/* create communication pipes */
		if ((pipe(p_status[i]) != 0) ||
			(pipe(p_command[i]) != 0) || (pipe(p_error[i]) != 0)) {
			fail(errno, "Could not create pipes!");
		}
		p_pid[i] = fork();
		/* todo: consider spawn() instead
		 * https://unix.stackexchange.com/questions/252901/get-output-of-posix-spawn
		 */

		if (0 > p_pid[i]) {
			fail(errno, "could not fork");
		}

		/* child process */
		if (0 == p_pid[i]) {
			if (dup2(p_command[i][0], STDIN_FILENO) != STDIN_FILENO) {
				fail(errno, "Could not dup stdin for player %i", i + 1);
			}

			if (dup2(p_status[i][1], STDOUT_FILENO) != STDOUT_FILENO) {
				fail(errno, "Could not dup stdout for player %i", i + 1);
			}

			if (dup2(p_error[i][1], STDERR_FILENO) != STDERR_FILENO) {
				fail(errno, "Could not dup stderr for player %i", i + 1);
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

	watchdog = 0;

	/* check if we can control the system's volume */
	control->volume = getVolume();
	switch (control->volume) {
	case MUTED:
		addMessage(MPV + 1, "Hardware volume is muted");
		break;
	case NOAUDIO:
		addMessage(0, "Hardware volume control is diabled");
		control->channel = NULL;
		break;
	default:
		addMessage(MPV + 1, "Hardware volume level is %i%%", control->volume);
	}

	for (int i = 0; i <= fading; i++) {
		pfd[2 * i].fd = p_status[i][0];
		pfd[2 * i].events = POLLIN;
		pfd[2 * i + 1].fd = p_error[i][0];
		pfd[2 * i + 1].events = POLLIN;
	}
	/* main loop
	 * TODO: nothing in this loop shall block so setting the watchdog will
	 * cause a restart in any case! */
	do {
		if ((poll(pfd, 2 * (fading + 1), 500) == 0) &&
			(control->mpmode & PM_STREAM) && (control->status != mpc_idle)) {

			/* status is not idle but we did not get any updates from either player
			 * after ten times we decide all hope is lost and we take the hard way
			 * out.
			 *
			 * this may happen when a stream is played and the network connection
			 * changes (most likely due to a DSL reconnect) or if the stream host
			 * fails. However those should trigger decode errors and lead an
			 * automatic restart, so this is really supposed to be the last
			 * failsafe. */

			if (++watchdog >= WATCHDOG_TIMEOUT) {
				addMessage(0, "Watchdog triggered!");
				return killPlayers(1);
			}
		}
		else {
			watchdog = 0;
		}

		if (fading) {
			/* drain inactive player */
			if (pfd[fdset ? 0 : 2].revents & POLLIN) {
				key = readline(line, MAXPATHLEN, p_status[fdset ? 0 : 1][0]);
				if (key > 2) {
					if ('@' == line[0]) {
						if (('F' != line[1]) && ('V' != line[1])
							&& ('I' != line[1])) {
							addMessage(MPV + 2, "P- %s", line);
						}

						switch (line[1]) {
						case 'R':	/* startup */
							addMessage(MPV + 1,
									   "MPG123 background instance is up");
							break;
						case 'E':
							if (control->current == NULL) {
								addMessage(0, "BG: %s", line);
							}
							else {
								/*  *INDENT-OFF*  */
								addMessage(-1, "BG: %s\n> Index: %i\n> Name: %s\n> Path: %s",
										   line, control->current->title->key,
										   control->current->title->display,
										   fullpath(control->current->title->path));
								/*  *INDENT-ON*  */
							}
							return killPlayers(1);
							break;
						case 'F':
							if (outvol > 0) {
								outvol--;
								snprintf(line, MAXPATHLEN, "volume %i\n",
										 outvol);
								toPlayer(1, line);
							}
							break;
						}
					}
					else {
						addMessage(MPV + 1, "OFF123: %s", line);
					}
				}
			}

			/* this shouldn't be happening but if it happens, it gives a hint */
			if (pfd[fdset ? 1 : 3].revents & POLLIN) {
				key = readline(line, MAXPATHLEN, p_error[fdset ? 0 : 1][0]);
				if (key > 1) {
					addMessage(-1, "BE: %s", line);
				}
			}
		}

		/* Interpret mpg123 output and ignore invalid lines */
		if ((pfd[fdset ? 2 : 0].revents & POLLIN) &&
			(3 < readline(line, MAXPATHLEN, p_status[fdset][0]))) {
			if ('@' == line[0]) {
				/* Don't print volume, tag and progress messages */
				if (('F' != line[1]) && ('V' != line[1]) && ('I' != line[1])) {
					addMessage(MPV + 2, "P+ %s", line);
				}
				/* Print tag info */
				if ('I' == line[1]) {
					addMessage(MPV + 3, "P+ %s", line);
				}
				switch (line[1]) {
					int32_t cmd;
					float rem;

					/* title/time position in status line */
					char *tpos;

				case 'R':		/* startup */
					addMessage(MPV + 1, "MPG123 foreground instance is up");
					break;

				case 'I':		/* ID3 info */
					/* ignore ID3 info on database play */
					if (control->mpmode & PM_DATABASE) {
						break;
					}

					/* sanity check */
					if (control->current == NULL) {
						fail(F_FAIL, "No current title list for stream!");
					}

					/* Stream name */
					if (NULL != strstr(line, "ICY-NAME: ")) {
						/* if prev is NULL it would mean the stream started before
						 * it was set - shouldn't ever happen */
						if (control->current->prev == NULL) {
							fail(F_FAIL, "Stream started on it's own!");
						}

						strip(control->current->prev->title->title,
							  line + 13, NAMELEN - 1);
						notifyChange(MPCOMM_TITLES);
					}

					/* metadata, usually title info */
					if (NULL != strstr(line, "ICY-META")) {
						/* StreamTitle='artist - title' -> apos[,tpos] */
						char *apos = strstr(line, "StreamTitle='");

						if (apos != NULL) {
							addMessage(MPV + 3, "%s", apos);
							apos = apos + strlen("StreamTitle='");

							/* find a proper terminating '; sequence */
							char *aend = strchr(apos, '\'');

							while (aend[1] != ';') {
								aend = strchr(aend + 1, '\'');
								if (aend == NULL) {
									/* unable to find '; at all */
									addMessage(0, "Format error in %s", apos);
									aend = apos + strlen(apos);
									break;
								}
							}
							*aend = 0;

							/* only do this if the title actually changed
							 * some streams mix up upper and lower cases in titles */
							if (strcasecmp
								(apos, control->current->title->display)) {

								/* The album field contains the stream name. If it is not set
								 * then this is the first title, and the current dummy will 
								 * just be overwriten. If the current title is some channel 
								 * info overwrite that too. Like this channel info will be 
								 * shown but not put in the history */
								if ((control->current->title->album[0] == '\0')
									||
									(strcasecmp
									 (control->current->title->artist,
									  control->current->title->album) == 0)) {
									strip(control->current->title->display,
										  apos, MAXPATHLEN - 1);
								}
								else {
									/* create a new title */
									control->current =
										addPLDummy(control->current, apos);
								}

								/* if possible cut up title and artist
								 * This fails if the artist has a ' - ' in the name but it's
								 * more likely that the title has a ' - ' so take the first
								 * dash not the last */
								tpos = strstr(apos, " - ");
								if (tpos != NULL) {
									*tpos = 0;
									strip(control->current->title->artist,
										  apos, NAMELEN - 1);
									strip(control->current->title->title,
										  tpos + 3, NAMELEN - 1);
								}
								/* can't find a title, so everything goes into the title
								 * line (this is centered on the display) */
								else {
									strip(control->current->title->title, apos,
										  NAMELEN - 1);
								}

								plCheck(false);
								/* carry over stream title as album entry */
								strcpy(control->current->title->album,
									   control->current->prev->title->title);

								/* filter out 'things' */
								if (strcasecmp
									(control->current->title->artist,
									 control->current->title->album) == 0) {
									/* mute news */
									if ((control->volume > 0) && (strcasecmp
																  (control->current->title->title,
																   "Nachrichten")
																  == 0)) {
										toggleMute();
										control->volume = AUTOMUTE;
									}
									/* unmute weather report */
									/*  *INDENT-OFF*  */
									if ((control->volume == AUTOMUTE) && 
										(strcasecmp (control->current->title->
													  title, "Wetter") == 0)) {
									/*  *INDENT-ON*  */					
									toggleMute();
								}
							}
							else if (control->volume == AUTOMUTE) {
								toggleMute();
							}
							notifyChange(MPCOMM_TITLES);
						}		/* title changed */
					}			/* stream title */
				}				/* ICY META */
				break;

case 'T':						/* TAG reply */
				addMessage(MPV + 1, "Got TAG reply?!");
				break;

case 'J':						/* JUMP reply */
				break;

case 'S':						/* Status message after loading a song (stream info) */
				break;

case 'F':						/* Status message during playing (frame info) */
				/* $1   = framecount (int32_t)
				 * $2   = frames left this song (int32_t)
				 * in  = seconds (float)
				 * rem = seconds left (float)
				 */
				tpos = strrchr(line, ' ');
				if (tpos == NULL) {
					addMessage(0, "Error in Frame info: %s", line);
					break;
				}
				rem = strtof(tpos, NULL);
				*tpos = 0;
				tpos = strrchr(line, ' ');
				if (tpos == NULL) {
					addMessage(0, "Error in Frame info: %s", line);
					break;
				}
				intime = strtof(tpos, NULL);

				/* fade in if needed */
				if (invol < 100) {
					invol++;
					snprintf(line, MAXPATHLEN, "volume %i\n", invol);
					toPlayer(0, line);
				}

				if (intime != oldtime) {
					oldtime = intime;
				}
				else {
					break;
				}

				control->playtime = (uint32_t) roundf(intime);
				/* file play */
				if (!(control->mpmode & PM_STREAM)) {
					control->percent = (100 * intime) / (rem + intime);
					control->remtime = (uint32_t) roundf(rem);

					/* we could just be switching from playlist to database */
					if (control->current == NULL) {
						if (!(control->mpmode & PM_SWITCH)) {
							addMessage(MPV + 1,
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
						playCount(control->current->title, p_skipped);
						p_skipped = 0;

						if (control->current->next == control->current) {
							control->status = mpc_idle;	/* Single song: STOP */
						}
						else {
							control->current = control->current->next;
							/* swap players */
							fdset = fdset ? 0 : 1;
							invol = 0;
							outvol = 100;
							toPlayer(0, "volume 0\n");
							sendplay();
							plCheck(true);
						}
					}
				}
				break;

case 'P':						/* Player status */
				cmd = atoi(&line[3]);

				switch (cmd) {
				case 0:		/* STOP */
					addMessage(MPV + 1, "FG Player stopped");
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
						addMessage(MPV + 1, "Stream stopped");
						/* stream stopped without user interaction */
						if (control->status == mpc_play) {
							addMessage(MPV + 1, "Trying to restart stream");
							control->status = mpc_start;
							sendplay();
						}
						else {
							control->status = mpc_idle;
						}
					}
					/* we're playing a playlist */
					else if ((control->current != NULL) &&
							 !(control->mpmode & PM_SWITCH)) {
						addMessage(MPV + 2, "Title change");
						if (p_order == 1) {
							if (playCount(control->current->title, p_skipped)
								== 1) {
								p_order = 0;
							}
							p_skipped = 0;
						}

						if (p_order < 0) {
							while ((control->current->prev != NULL)
								   && p_order < 0) {
								control->current = control->current->prev;
								p_order++;
							}
							/* ignore skip before first title in playlist */
						}

						if (p_order > 0) {
							while ((control->current->next != NULL)
								   && p_order > 0) {
								control->current = control->current->next;
								p_order--;
							}
							/* stop on end of playlist */
							if (p_order > 0) {
								control->status = mpc_idle;	/* stop */
							}
						}

						if (control->status != mpc_idle) {
							sendplay();
						}

						if (control->mpmode == PM_DATABASE) {
							plCheck(true);
						}
					}
					/* always re-enable proper playorder after stop */
					p_order = 1;
					break;

				case 1:		/* PAUSE */
					control->status = mpc_pause;
					break;

				case 2:		/* PLAY */
					if (control->mpmode & PM_SWITCH) {
						addMessage(MPV + 1,
								   "Playing profile #%" PRIu32 ": %s",
								   control->active,
								   getProfileName(control->active));
						control->mpmode &= ~PM_SWITCH;
						if (control->active != 0) {
							writeConfig(NULL);
							oactive = control->active;
						}
					}
					control->status = mpc_play;
					notifyChange(MPCOMM_CONFIG);
					break;

				default:
					addMessage(0, "Unknown status %i on FG player!\n%s",
							   cmd, line);
					break;
				}
				break;

case 'V':						/* volume reply */
				break;

case 'E':
				addMessage(0, "FG: %s!", line + 3);
				if (control->current != NULL) {
					if (control->mpmode & PM_STREAM) {
						addMessage(MPV + 1,
								   "FG: %s <- %s\n Name: %s\n URL: %s",
								   getProfileName(control->active),
								   getProfileName(oactive),
								   control->current->title->display,
								   control->streamURL);
					}
					else {
							/*  *INDENT-OFF*  */
							addMessage(MPV+1, "FG: %i\n> Name: %s\n> Path: %s",
									   control->current->title->key,
									   control->current->title->display,
									   fullpath(control->current->title->path));
							/*  *INDENT-ON*  */
					}
				}
				return killPlayers(1);
				break;

default:
				addMessage(0, "Warning: %s", line);
				break;
			}					/* case line[1] */
		}						/* if line starts with '@' */
		else {
			/* verbosity 1 as sometimes tags appear here which confuses on level 0 */
			addMessage(MPV + 1, "Raw: %s", line);
		}
	}							/* FD_ISSET( p_status ) */

	if (pfd[fdset ? 3 : 1].revents & POLLIN) {
		key = readline(line, MAXPATHLEN, p_error[fdset][0]);
		if (key > 1) {
			if (strstr(line, "rror: ")) {
				addMessage(0, "%s", line);
				return killPlayers(1);
			}
			else if (strstr(line, "Warning: ") == line) {
				/* ignore content-type warnings */
				if (!strstr(line, "content-type")) {
					addMessage(0, "FE: %s", line);
				}
			}
			else {
				addMessage(MPV + 1, "FE: %s", line);
			}
		}
	}
	updateUI();
	}
	while ((control->status != mpc_quit) && (control->status != mpc_reset));

	dbWrite(0);

	return NULL;
	}

#undef MPV
