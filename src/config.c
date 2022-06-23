/*
 * config.c
 *
 * handles reading and writing the configuration
 *
 * the configuration file looks like a standard GTK configuration for
 * historical reasons. It should also parse with the gtk_* functions but those
 * are not always available in headless environments.
 *
 *  Created on: 16.11.2017
 *	  Author: bweber
 */
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>

#include "utils.h"
#include "config.h"
#include "musicmgr.h"
#include "mpcomm.h"

static pthread_mutex_t pllock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t conflock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t confinit = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t _addmsglock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t _cblock = PTHREAD_MUTEX_INITIALIZER;
static mpconfig_t *_cconfig = NULL;

#define MPV 10

/**
 * the progress function list
 */
typedef struct _mpfunc_t _mpfunc;
struct _mpfunc_t {
	void (*func)(void *);
	void *arg;
	_mpfunc *next;
};

/* callback hooks */
static _mpfunc *_ufunc = NULL;

/* notification states per client */
static uint32_t _notify[MAXCLIENT];

static const char *mpccommand[] = {
	"play",
	"stop",
	"prev",
	"next",
	"start",
	"repl",
	"profile",
	"quit",
	"dbclean",
	"fav",
	"dnp",
	"doublets",
	"insert",
	"ivol",
	"dvol",
	"bskip",
	"fskip",
	"move",
	"dbinfo",
	"search",
	"append",
	"setvol",
	"newprof",
	"path",
	"remprof",
	"searchmode",
	"deldnp",
	"delfav",
	"remove",
	"mute",
	"favplay",
	"reset",
	"pause",
	"clone",
	"idle"
};

inline void lockPlaylist(void) {
	pthread_mutex_lock(&pllock);
}

inline void unlockPlaylist(void) {
	pthread_mutex_unlock(&pllock);
}

inline int32_t trylockPlaylist(void) {
	return (pthread_mutex_trylock(&pllock) != EBUSY);
}

static void invokeHooks(_mpfunc * hooks) {
	_mpfunc *pos = hooks;

	pthread_mutex_lock(&_cblock);
	while (pos != NULL) {
		pos->func(pos->arg);
		pos = pos->next;
	}
	pthread_mutex_unlock(&_cblock);
}

/*
 * transform an mpcmd value into a string literal
 */
const char *mpcString(mpcmd_t rawcmd) {
	mpcmd_t cmd = MPC_CMD(rawcmd);

	if (cmd <= mpc_idle) {
		return mpccommand[cmd];
	}
	else {
		addMessage(MPV + 1, "Unknown command code %i", cmd);
		return "mpc_idle";
	}
}

/*
 * transform a string literal into an mpcmd value
 */
mpcmd_t mpcCommand(const char *name) {
	int32_t i;

	for (i = 0; i <= mpc_idle; i++) {
		if (strstr(name, mpccommand[i]))
			break;
	}
	if (i > mpc_idle) {
		addMessage(MPV + 1, "Unknown command %s!", name);
		return mpc_idle;
	}
	return (mpcmd_t) i;
}

/**
 * returns the current configuration
 */
mpconfig_t *getConfig() {
	pthread_mutex_lock(&conflock);
	if (_cconfig == NULL) {
		pthread_cond_wait(&confinit, &conflock);
	}
	pthread_mutex_unlock(&conflock);
	return _cconfig;
}

inline mpplaylist_t *getCurrent() {
	return getConfig()->current;
}

/**
 * parses a multi-string config value in the form of:
 * val1;val2;val3;
 *
 * returns the number of found values
 */
static int32_t scanparts(char *line, char ***target) {
	uint32_t i;
	char *pos;
	uint32_t num = 0;

	/* count number of entries */
	for (i = 0; i < strlen(line); i++) {
		if (line[i] == ';') {
			num++;
		}
	}

	/* walk through the entries */
	if (num > 0) {
		*target = (char **) falloc(num, sizeof (char *));
		for (i = 0; i < num; i++) {
			pos = strchr(line, ';');
			if (pos != NULL) {
				*pos = 0;
				(*target)[i] = (char *) falloc(strlen(line) + 1, 1);
				strip((*target)[i], line, strlen(line));
				line = pos + 1;
			}
			else {
				fail(F_FAIL, "Config file Format error in %s!", line);
			}
		}
	}

	return num;
}

/*
 * scans a number on names into newly created profile entries
 * this must happen before urls or volumes get parsed!
 */
static int32_t scanpronames(char *input, profile_t *** target, int32_t max) {
	char **line;
	int32_t i, num;

	num = scanparts(input, &line);
	if (max > 0) {
		for (i = 0; i < MIN(max, num); i++) {
			(*target)[i]->name = line[i];
		}
		free(line);
		max = (max == num);
	}
	else if (num > 0) {
		*target = (profile_t **) falloc(num, sizeof (profile_t *));

		for (i = 0; i < num; i++) {
			if (line[i][1] == ':') {
				(*target)[i] = createProfile(line[i] + 2, NULL, 0, -1);
				if (line[i][0] == '1') {
					(*target)[i]->favplay = 1;
				}
			}
			else {
				(*target)[i] = createProfile(line[i], NULL, 0, -1);
			}
			free(line[i]);
		}
		free(line);
		max = num;
	}
	return max;
}

static int32_t scanpropaths(char *input, profile_t *** target) {
	char **line;
	int32_t i, num;

	num = scanparts(input, &line);

	if (num > 0) {
		*target = (profile_t **) falloc(num, sizeof (profile_t *));

		for (i = 0; i < num; i++) {
			(*target)[i] = createProfile(NULL, line[i], 0, -1);
			free(line[i]);
		}
		free(line);
	}

	return num;
}

static int32_t scanprovols(char *input, profile_t ** target, int32_t max) {
	char **line;
	int32_t i, num;

	num = scanparts(input, &line);
	for (i = 0; i < MIN(max, num); i++) {
		target[i]->volume = atoi(line[i]);
		free(line[i]);
	}
	free(line);

	return (num == max);
}

static int32_t scancodes(char *input, int32_t * codes) {
	char **line;
	int32_t i, num;

	num = scanparts(input, &line);
	if (num > 0) {
		if (num == MPRC_NUM) {
			for (i = 0; i < MPRC_NUM; i++) {
				codes[i] = atoi(line[i]);
			}
		}
		for (i = 0; i < num; i++) {
			free(line[i]);
		}
		free(line);
	}
	return num;
}

/**
 * reads the configuration file at $HOME/.mixplay/mixplay.conf and stores the settings
 * in the given control structure.
 * returns NULL is no configuration file exists
 *
 * This function should be called more or less first thing in the application
 */
mpconfig_t *readConfig(void) {
	char conffile[MAXPATHLEN + 1];	/*  = "mixplay.conf"; */
	char *line;
	char *pos;
	char *home = NULL;
	FILE *fp;
	int32_t i;

	home = getenv("HOME");
	if (home == NULL) {
		fail(F_FAIL, "Cannot get HOME!");
	}
	pthread_mutex_lock(&conflock);
	if (_cconfig == NULL) {
		_cconfig = (mpconfig_t *) falloc(1, sizeof (mpconfig_t));
		_cconfig->msg = msgBuffInit();
		_cconfig->found =
			(searchresults_t *) falloc(1, sizeof (searchresults_t));
		_cconfig->found->state = mpsearch_idle;
	}
	else {
		/* Happens on the first run! */
		addMessage(-1, "Config being read twice!");
	}

	/* Set some default values */
	_cconfig->root = NULL;
	_cconfig->current = NULL;
	_cconfig->volume = 80;
	_cconfig->pvolume = 80;
	_cconfig->active = 1;
	_cconfig->playtime = 0;
	_cconfig->remtime = 0;
	_cconfig->percent = 0;
	_cconfig->status = mpc_idle;
	_cconfig->dbname = (char *) falloc(MAXPATHLEN + 1, 1);
	_cconfig->password = strdup("mixplay");
	_cconfig->skipdnp = 3;
	_cconfig->sleepto = 0;
	_cconfig->debug = 0;
	_cconfig->fade = FADESECS;
	_cconfig->inUI = 0;
	_cconfig->msg->lines = 0;
	_cconfig->msg->current = 0;
	_cconfig->port = MP_PORT;
	_cconfig->isDaemon = 0;
	_cconfig->searchDNP = 0;
	_cconfig->streamURL = NULL;
	_cconfig->rcdev = NULL;
	_cconfig->mpmode = PM_NONE;

	snprintf(_cconfig->dbname, MAXPATHLEN, "%s/.mixplay/mixplay.db", home);

	snprintf(conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home);
	fp = fopen(conffile, "r");

	if (fp == NULL) {
		pthread_mutex_unlock(&conflock);
		return createConfig();
	}
	else {
		do {
			if ((line = fetchline(fp)) == NULL) {
				continue;
			}
			if (line[0] == '#') {
				free(line);
				continue;
			}
			pos = strchr(line, '=');
			if ((NULL == pos) || (strlen(++pos) == 0)) {
				free(line);
				continue;
			}

			if (strstr(line, "musicdir=") == line) {
				/* make sure that musicdir ends with a '/' */
				if (line[strlen(line) - 1] == '/') {
					_cconfig->musicdir = strdup(pos);
				}
				else {
					_cconfig->musicdir = (char *) falloc(strlen(pos) + 2, 1);
					strcpy(_cconfig->musicdir, pos);
					_cconfig->musicdir[strlen(pos)] = '/';
				}
				if (!isDir(_cconfig->musicdir)) {
					printf("%s is no valid directory!\n", _cconfig->musicdir);
					exit(ENOENT);
				}
			}
			if (strstr(line, "password=") == line) {
				_cconfig->password = strdup(pos);
			}
			if (strstr(line, "channel=") == line) {
				_cconfig->channel = strdup(pos);
			}
			if (strstr(line, "profiles=") == line) {
				_cconfig->profiles = scanpronames(pos, &_cconfig->profile, -1);
			}
			if (strstr(line, "volume=") == line) {
				_cconfig->pvolume = atoi(pos);
			}
			if (strstr(line, "streams=") == line) {
				_cconfig->streams = scanpropaths(pos, &_cconfig->stream);
			}
			if (strstr(line, "snames=") == line) {
				if (!scanpronames(pos, &_cconfig->stream, _cconfig->streams)) {
					fclose(fp);
					fail(F_FAIL,
						 "Number of streams and stream names does not match!");
				}
			}
			if (strstr(line, "svolumes=") == line) {
				if (!scanprovols(pos, _cconfig->stream, _cconfig->streams)) {
					addMessage(0, "Number of stream volumes does not match!");
				}
			}
			if (strstr(line, "active=") == line) {
				_cconfig->active = atoi(pos);
				if (_cconfig->active == 0) {
					addMessage(0, "Setting default profile!");
					_cconfig->active = 1;
				}
			}
			if (strstr(line, "skipdnp=") == line) {
				_cconfig->skipdnp = atoi(pos);
			}
			if (strstr(line, "sleepto=") == line) {
				_cconfig->sleepto = atoi(pos);
			}
			if (strstr(line, "fade=") == line) {
				_cconfig->fade = atoi(pos);
			}
			if (strstr(line, "port=") == line) {
				_cconfig->port = atoi(pos);
			}
			if (strstr(line, "rcdev=") == line) {
				_cconfig->rcdev = (char *) falloc(strlen(pos) + 1, 1);
				strip(_cconfig->rcdev, pos, strlen(pos));
			}
			if (strstr(line, "rccodes=") == line) {
				if (scancodes(pos, _cconfig->rccodes) != MPRC_NUM) {
					fclose(fp);
					fail(F_FAIL, "Wrong number of RC codes!");
				}
			}
			free(line);
		}
		while (!feof(fp));

		fclose(fp);

		if (_cconfig->profiles == 0) {
			fail(F_FAIL, "No profiles defined!");
		}
	}

	pthread_cond_signal(&confinit);
	pthread_mutex_unlock(&conflock);

	for (i = 0; i < MAXCLIENT; i++) {
		_notify[i] = MPCOMM_STAT;
	}

	return _cconfig;
}

/**
 * writes the configuration from the given control structure into the file at
 * $HOME/.mixplay/mixplay.conf
 */
void writeConfig(const char *musicpath) {
	char conffile[MAXPATHLEN + 1];	/*  = "mixplay.conf"; */
	int32_t i;
	FILE *fp;
	char *home = NULL;

	addMessage(MPV + 1, "Saving config");
	assert(_cconfig != NULL);

	home = getenv("HOME");
	if (home == NULL) {
		fail(F_FAIL, "Cannot get HOME!");
	}

	if (musicpath != NULL) {
		_cconfig->musicdir = (char *) falloc(strlen(musicpath) + 2, 1);
		strip(_cconfig->musicdir, musicpath, strlen(musicpath) + 1);
		if (_cconfig->musicdir[strlen(_cconfig->musicdir) - 1] != '/') {
			strtcat(_cconfig->musicdir, "/", strlen(musicpath) + 2);
		}
	}

	snprintf(conffile, MAXPATHLEN, "%s/.mixplay", home);
	if (mkdir(conffile, 0700) == -1) {
		if (errno != EEXIST) {
			fail(errno, "Could not create config dir %s", conffile);
		}
	}

	snprintf(conffile, MAXPATHLEN, "%s/.mixplay/mixplay.conf", home);

	fp = fopen(conffile, "w");

	if (NULL != fp) {
		fprintf(fp, "[mixplay]");
		fprintf(fp, "\nmusicdir=%s", _cconfig->musicdir);
		fprintf(fp, "\npassword=%s", _cconfig->password);
		if (_cconfig->profiles == 0) {
			fprintf(fp, "\nactive=1");
			fprintf(fp, "\nprofiles=0:mixplay;");
			fprintf(fp, "\nvolume=80;");
		}
		else {
			fprintf(fp, "\nactive=%i", _cconfig->active);
			fprintf(fp, "\nprofiles=");
			for (i = 0; i < _cconfig->profiles; i++) {
				fprintf(fp, "%i:%s;", _cconfig->profile[i]->favplay,
						_cconfig->profile[i]->name);
			}
		}
		fprintf(fp, "\nvolume=");
		if (_cconfig->active > 0) {
			fprintf(fp, "%" PRId32 ";", _cconfig->volume);
		}
		else {
			fprintf(fp, "%" PRId32 ";", _cconfig->pvolume);
		}
		fprintf(fp, "\nstreams=");
		for (i = 0; i < _cconfig->streams; i++) {
			fprintf(fp, "%s;", _cconfig->stream[i]->stream);
		}
		fprintf(fp, "\nsnames=");
		for (i = 0; i < _cconfig->streams; i++) {
			fprintf(fp, "%s;", _cconfig->stream[i]->name);
		}
		fprintf(fp, "\nsvolumes=");
		for (i = 0; i < _cconfig->streams; i++) {
			fprintf(fp, "%i;", _cconfig->stream[i]->volume);
		}
		fprintf(fp, "\nskipdnp=%i", _cconfig->skipdnp);
		fprintf(fp, "\nsleepto=%i", _cconfig->sleepto);
		fprintf(fp, "\nfade=%i", _cconfig->fade);
		if (_cconfig->channel != NULL) {
			fprintf(fp, "\nchannel=%s", _cconfig->channel);
		}
		else {
			fprintf(fp, "\nchannel=Master");
			fprintf(fp, "\n# channel=Digital for HifiBerry");
			fprintf(fp, "\n# channel=Main");
			fprintf(fp, "\n# channel=DAC");
		}
		if (_cconfig->port != MP_PORT) {
			fprintf(fp, "\nport=%i", _cconfig->port);
		}
		if (_cconfig->rcdev != NULL) {
			fprintf(fp, "\nrcdev=%s", _cconfig->rcdev);
			fprintf(fp, "\nrccodes=");
			for (i = 0; i < MPRC_NUM; i++) {
				fprintf(fp, "%i;", _cconfig->rccodes[i]);
			}
		}
		fprintf(fp, "\n");
		fclose(fp);
	}
	else {
		fail(errno, "Could not open %s", conffile);
	}
}

mpconfig_t *createConfig() {
	char path[MAXPATHLEN];

	printf("music directory needs to be set.\n");
	printf("It will be set up now\n");
	while (1) {
		printf("Default music directory:");
		fflush(stdout);
		memset(path, 0, MAXPATHLEN);
		if (fgets(path, MAXPATHLEN, stdin) == NULL) {
			printf("Got nothing..\n");
			continue;
		};
		printf("Got %s\n", path);
		path[strlen(path) - 1] = 0;	/* cut off CR */
		abspath(path, getenv("HOME"), MAXPATHLEN);

		if (isDir(path)) {
			break;
		}
		else {
			printf("%s is not a directory!\n", path);
		}
	}
	printf("Writing config..\n");
	writeConfig(path);
	/* make sure we really wrote a valid config */
	printf("Reading config..\n");
	return readConfig();
}

void freeProfile(profile_t * profile) {
	if (profile != NULL) {
		if (profile->name != NULL) {
			free(profile->name);
		}
		if (profile->stream != NULL) {
			free(profile->stream);
		}
		free(profile);
	}
}

/**
 * frees the static parts of the config
 */
void freeConfigContents() {
	int32_t i;

	assert(_cconfig != NULL);

	sfree(&(_cconfig->dbname));
	sfree(&(_cconfig->musicdir));

	for (i = 0; i < _cconfig->profiles; i++) {
		freeProfile(_cconfig->profile[i]);
	}
	_cconfig->profiles = 0;
	sfree((char **) &(_cconfig->profile));

	for (i = 0; i < _cconfig->streams; i++) {
		freeProfile(_cconfig->stream[i]);
	}
	_cconfig->streams = 0;
	sfree((char **) &(_cconfig->stream));
	sfree((char **) &(_cconfig->channel));
	sfree((char **) &(_cconfig->password));

	msgBuffDiscard(_cconfig->msg);
}

/*
 * wrapper to clean up player. If no database was loaded, the titles in the
 * playlist should be free()'d  too. If a database is loaded the titles in
 * the playlist must not be free()'d
 */
static void wipePTLists(mpconfig_t * control) {
	if (control->root != NULL) {
		control->root = wipeTitles(control->root);
		control->current = wipePlaylist(control->current, 0);
	}
	else {
		control->current = wipePlaylist(control->current, 1);
	}
}

/**
 * recursive free() to clean up all of the configuration
 */
void freeConfig() {
	assert(_cconfig != NULL);
	freeConfigContents();
	_cconfig->found->titles = wipePlaylist(_cconfig->found->titles, 0);
	wipePTLists(_cconfig);
	if (_cconfig->found->artists != NULL) {
		free(_cconfig->found->artists);
	}
	if (_cconfig->found->albums != NULL) {
		free(_cconfig->found->albums);
	}
	if (_cconfig->found->albart != NULL) {
		free(_cconfig->found->albart);
	}
	free(_cconfig->found);
	wipeList(_cconfig->dnplist);
	wipeList(_cconfig->favlist);

	free(_cconfig);
	_cconfig = NULL;
}

/**
 * adds a message
 *
 * v controls the verbosity
 * -1 - message is an alert, treat as 0 furtheron
 *  0 - add to message buffer, write to syslog if daemonized
 *  n - if debuglevel is > n print on screen
 */
void addMessage(int32_t v, const char *msg, ...) {
	va_list args;
	char *line;

	pthread_mutex_lock(&_addmsglock);
	line = (char *) falloc(MP_MSGLEN + 1, 1);
	va_start(args, msg);
	vsnprintf(line, MP_MSGLEN, msg, args);
	va_end(args);

	/* cut off trailing linefeeds */
	if (line[strlen(line)] == '\n') {
		line[strlen(line)] = 0;
	}

	if (v < 1) {
		/* normal status messages */
		if (v == -1) {
			memmove(line + 6, line, MP_MSGLEN - 6);
			memcpy(line, "ALERT:", 6);
			line[MP_MSGLEN] = 0;
		}
		if (_cconfig->inUI) {
			msgBuffAdd(_cconfig->msg, line);
		}
		if (_cconfig->isDaemon) {
			syslog(LOG_NOTICE, "%s", line);
		}
	}

	if (v < getDebug()) {
		fprintf(stderr, "\r%s\n", line);
	}

	free(line);
	pthread_mutex_unlock(&_addmsglock);
}

void incDebug(void) {
	assert(_cconfig != NULL);
	_cconfig->debug++;
}

/**
 * returns the current debuglevel. If the configuration has not yet been
 * initialized, a debug level of 1 (HID-mode) is assumed.
 */
int32_t getDebug(void) {
	if (_cconfig == NULL) {
		return 1;
	}
	return _cconfig->debug;
}

/**
 * returns true id debug is enabled, that means debug > 1!
 * debug == 1 only means that console is enabled
 */
bool isDebug(void) {
	if (_cconfig == NULL) {
		return true;
	}
	return (_cconfig->debug > 1);
}

/* returns true if the player is not normally playing */
int32_t playerIsBusy(void) {
	int32_t res = 0;
	mpconfig_t *control = getConfig();

	res = (control->status == mpc_start) || (control->mpmode & PM_SWITCH);
	return res;
}

#define MP_ACTLEN 75
static char _curact[MP_ACTLEN + 1] = "startup";

char *getCurrentActivity(void) {
	return _curact;
}

/*
 * set the current activity
 * the given string is shown as title for the client while the playlist
 * is either locked or not yet set.
 * Print message in debug interface when debuglevel is < n, so -1 means
 * the activity is not shown in debuginterface
 */
void activity(int32_t v, const char *act) {
	if (strcmp(act, _curact)) {
		strtcpy(_curact, act, MP_ACTLEN);
		notifyChange(MPCOMM_TITLES);
		if (getDebug() >= v) {
			printf("\r* %s\r", _curact);
		}
	}
}

static void addHook(void (*func)(void *), void *arg, _mpfunc ** list) {
	_mpfunc *pos = *list;

	pthread_mutex_lock(&_cblock);
	if (pos == NULL) {
		*list = (_mpfunc *) falloc(1, sizeof (_mpfunc));
		pos = *list;
	}
	else {
		while (pos->next != NULL) {
			pos = pos->next;
		}
		pos->next = (_mpfunc *) falloc(1, sizeof (_mpfunc));
		pos = pos->next;
	}
	pos->func = func;
	pos->arg = arg;
	pos->next = NULL;
	pthread_mutex_unlock(&_cblock);
}

#if 0
static void removeHook(void (*func)(void *), void *arg, _mpfunc ** list) {
	_mpfunc *pos = *list;
	_mpfunc *pre = NULL;

	pthread_mutex_lock(&_cblock);

	if (pos == NULL) {
		addMessage(0, "Empty callback list!");
		pthread_mutex_unlock(&_cblock);
		return;
	}

	/* does the callback to be removed lead the list? */
	if ((pos->func == func) && (pos->arg == arg)) {
		*list = pos->next;
		free(pos);
	}
	/* step through the rest of the list */
	else {
		while (pos->next != NULL) {
			pre = pos;
			pos = pos->next;
			if ((pos->func == func) && (pos->arg == arg)) {
				pre->next = pos->next;
				free(pos);
				break;
			}
		}
	}

	pthread_mutex_unlock(&_cblock);
}
#endif

/**
 * register an update function, called on minor updates like playtime
 */
void addUpdateHook(void (*func)(void *)) {
	addHook(func, NULL, &_ufunc);
}

/**
 * notify all clients that a bigger update is needed
 */
void notifyChange(int32_t state) {
	int32_t i;

	pthread_mutex_lock(&_cblock);
	for (i = 0; i < MAXCLIENT; i++) {
		addNotify(i, state);
	}
	pthread_mutex_unlock(&_cblock);
}

/**
 * run all registered update functions
 */
void updateUI() {
	invokeHooks(_ufunc);
}

/*
 * returns a pointer to a string containing a full absolute path to the file
 * CAVEAT: This is not thread safe at all!
 */
char *fullpath(const char *file) {
	static char pbuff[MAXPATHLEN + 1];

	pbuff[0] = 0;
	if (file[0] != '/') {
		strtcpy(pbuff, getConfig()->musicdir, MAXPATHLEN);
		strtcat(pbuff, file, MAXPATHLEN);
	}
	else {
		strtcpy(pbuff, file, MAXPATHLEN);
	}
	return pbuff;
}

int32_t getFavplay() {
	if (getConfig()->active > 0) {
		return getConfig()->profile[getConfig()->active - 1]->favplay;
	}
	return 0;
}

int32_t toggleFavplay() {
	profile_t *profile;

	if (getConfig()->active > 0) {
		profile = getConfig()->profile[getConfig()->active - 1];
		profile->favplay = !profile->favplay;
		return profile->favplay;
	}
	return 0;
}

profile_t *createProfile(const char *name, const char *stream,
						 const uint32_t favplay, const int32_t vol) {
	profile_t *profile = (profile_t *) falloc(1, sizeof (profile_t));

	if (name) {
		profile->name = strdup(name);
	}
	else {
		profile->name = NULL;
	}
	if (stream) {
		profile->stream = strdup(stream);
	}
	else {
		profile->stream = NULL;
	}
	profile->favplay = favplay;
	profile->volume = vol;
	return profile;
}

/**
 * deletes the current playlist
 * this is not in musicmanager.c to keep cross-dependecies in
 * config.c low
 */
mpplaylist_t *wipePlaylist(mpplaylist_t * pl, int32_t recursive) {
	mpplaylist_t *next = NULL;

	if (pl == NULL) {
		return NULL;
	}

	int32_t unlock = trylockPlaylist();

	while (pl->prev != NULL) {
		pl = pl->prev;
	}

	while (pl != NULL) {
		next = pl->next;
		pl->title->flags &= ~MP_INPL;
		if (recursive) {
			free(pl->title);
		}
		free(pl);
		pl = next;
	}
	if (unlock)
		unlockPlaylist();
	return NULL;
}

/**
 * discards a list of markterms and frees the memory
 * returns NULL for intuitive calling
 */
marklist_t *wipeList(marklist_t * root) {
	marklist_t *runner = root;

	if (root == NULL) {
		return NULL;
	}

	lockPlaylist();
	if (NULL != root) {
		while (runner != NULL) {
			root = runner->next;
			free(runner);
			runner = root;
		}
	}
	unlockPlaylist();

	return NULL;
}

/**
 * discards a list of titles and frees the memory
 * returns NULL for intuitive calling
 */
mptitle_t *wipeTitles(mptitle_t * root) {
	mptitle_t *runner = root;

	if (NULL != root) {
		root->prev->next = NULL;

		activity(2, "Cleaning");
		while (runner != NULL) {
			root = runner->next;
			free(runner);
			runner = root;
		}
	}

	return NULL;
}

/*
 * block all signals that would interrupt the execution flow
 * Yes, this is bad practice, yes this will become a signal handler thread
 */
void blockSigint() {
	sigset_t set;

	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	sigaddset(&set, SIGTERM);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL) != 0) {
		addMessage(0, "Could not block SIGINT");
	}
}

int32_t getNotify(int32_t client) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		return _notify[client];
	}
	return 0;
}

void addNotify(int32_t client, int32_t state) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		_notify[client] |= state;
	}
}

void clearNotify(int32_t client) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		_notify[client] = MPCOMM_STAT;
	}
}

static uint64_t _msgcnt[MAXCLIENT];

uint64_t getMsgCnt(int32_t client) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		return _msgcnt[client];
	}
	return 0;
}

void setMsgCnt(int32_t client, uint64_t count) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		_msgcnt[client] = count;
	}
}

void incMsgCnt(int32_t client) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		_msgcnt[client]++;
	}
}

void initMsgCnt(int32_t client) {
	client--;
	if ((client >= 0) && (client < MAXCLIENT)) {
		_msgcnt[client] = msgBufGetLastRead(getConfig()->msg);
	}
}

#undef MPV
