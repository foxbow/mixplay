/* unittest stub */
/* use this to test single functions from the commandline
 * 'make bin/test' */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>

#include "musicmgr.h"

/*
 * Print errormessage and exit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *		 F_FAIL = print message w/o errno and exit
 */
void fail(const int32_t error, const char *msg, ...) {
	va_list args;

	fprintf(stdout, "\n");
	printf("test: ");
	va_start(args, msg);
	vfprintf(stdout, msg, args);
	va_end(args);
	fprintf(stdout, "\n");
	if (error > 0) {
		fprintf(stdout, "ERROR: %i - %s\n", abs(error), strerror(abs(error)));
	}
	exit(0);
}

int32_t main(int32_t argc, char **argv) {
	int32_t res = 0;
	mpconfig_t *config = readConfig();

	config->debug = 9;

	if (argc < 3) {
		printf("Gib arguments!\n");
		return 0;
	}

	res = checkSim(argv[1], argv[2]);
	if (res) {
		printf("%s and %s are similar\n", argv[1], argv[2]);
	}
	else {
		printf("%s and %s are not similar\n", argv[1], argv[2]);
	}
	return res;
}
