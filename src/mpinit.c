#include <stdio.h>
#include <unistd.h>

#include "musicmgr.h"
#include "utils.h"
#include "player.h"
#include "mpserver.h"

/*
 * print out the default CLI help text
 */
static void printUsage(char *name) {
	printf("USAGE: %s [args] [resource]\n", name);
	printf(" -V : print curent build ID\n");
	printf(" -d : increase debug message level to display on console\n");
	printf(" -f : single channel - disable fading\n");
	printf(" -F <seconds> : enable crossfading\n");
	printf(" -h : print help\n");
	printf(" -p <port> : set port [2347]\n");
	printf(" -m : force mix on playlist\n");
	printf(" -W': write changed config (used with -r,-l,-h,-p)\n");
	printf(" resource: resource to play\n");
	printf("		   URL, directory, mp3 file, playlist\n");
}

/**
 * parse arguments given to the application
 * also handles playing of a single file, a directory, a playlist or an URL
 * this is also called after initialization, so the PM_SWITCH flag does
 * actually make sense here.
 */
int setArgument(const char *arg) {
	char line[MAXPATHLEN + 1];
	mpconfig_t *control = getConfig();

	control->active = 0;
	control->mpmode = PM_NONE;

	if (isURL(arg)) {
		addMessage(1, "URL: %s", arg);
		line[0] = 0;

		if (strstr(arg, "https") == arg) {
			addMessage(0, "No HTTPS support, trying plain HTTP.");
			strtcat(line, "http", MAXPATHLEN);
			strtcat(line, arg + 5, MAXPATHLEN);
		}
		else {
			strtcpy(line, arg, MAXPATHLEN);
		}
		setStream(line, "<connecting>");
		return 1;
	}

	addMessage(-1, "Illegal argument '%s'!", arg);
	return 0;
}

/*
 * parses the given flags and arguments
 */
int getArgs(int argc, char **argv) {
	mpconfig_t *config = getConfig();
	int c, changed = 0;

	/* parse command line options */
	/* using unsigned char c to work around getopt quirk on ARM */
	while ((c = getopt(argc, argv, "vVfdF:h:p:Wm")) != -1) {
		switch (c) {

		case 'V':
			printf("%s version %s\n", argv[0], VERSION);
			exit(EXIT_SUCCESS);

		case 'd':				/* increase debug message level to display on console */
			incDebug();
			break;

		case 'f':				/* single channel - disable fading */
			config->fade = 0;
			break;

		case 'F':				/* enable fading */
			config->fade = atoi(optarg);
			if (config->fade < 1) {
				printf("Invalid number of seconds for -F (%s)\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'h':
			printUsage(argv[0]);
			exit(0);
			break;

		case 'p':
			config->port = atoi(optarg);
			break;

		case 'W':
			changed = 1;
			break;

		case '?':
			switch (optopt) {
			case 'h':
			case 'p':
			case 'F':
				fprintf(stderr, "Option -%c requires an argument!\n", optopt);
				break;
			default:
				printf("Unknown option -%c\n", optopt);
				break;
			}
			/* fallthrough */

		default:
			printUsage(argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		return setArgument(argv[optind]);
	}

	if (changed) {
		writeConfig(NULL);
	}
	return 0;
}

/**
 * the control thread to communicate with the mpg123 processes
 * should be triggered after the app window is realized
 *
 * this will also start the communication thread is remote=2
 */
int initAll() {
	mpconfig_t *control;
	pthread_t tid;
	struct timespec ts;

	control = getConfig();
	ts.tv_sec = 0;
	ts.tv_nsec = 250000;

	/* start the actual player */
	pthread_create(&control->rtid, NULL, reader, NULL);
	/* make sure the mpg123 instances have a chance to start up */
	nanosleep(&ts, NULL);

	/* Runs as thread to have updates in the UI */
	pthread_create(&tid, NULL, setProfile, NULL);
	pthread_detach(tid);

	return 0;
}
