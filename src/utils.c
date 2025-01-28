/**
 * collection of all-purpose utility functions
 */
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <termios.h>
#include <assert.h>
#include <linux/input.h>
#include <poll.h>

#include "utils.h"

/**
 * checks if a character is a divisor
 * used in patPrep to remove 'fancy' characters before
 * checking similarity of strings
 */
static bool isdiv(const char c) {
	switch (c) {
	case '-':
	case '/':
	case '.':
	case ',':
	case ':':
	case ';':
	case '&':
	case '+':
	case '*':
	case '(':
	case ')':
	case '[':
	case ']':
		return true;
		break;
	}
	return false;
}

/**
 * prepares the text in src for comparing, meaning:
 * 1. multiple whitespaces are combined into one
 * 2. divisors are removed (-,.:;/)
 * 3. text is turned lowercase
 * 
 * returns the length of the resulting string
 */
static int patPrep(char *tgt, const char *src, int len) {
	int tpos = 0;
	bool sflag = false;

	for (int pos = 0; pos < len; pos++) {
		// reduce whitespaces
		if (isspace(src[pos])) {
			if (sflag)
				continue;
			sflag = true;
		}
		else
			sflag = false;

		// skip divisors
		if (isdiv(src[pos]))
			continue;

		tgt[tpos++] = tolower(src[pos]);
	}

	tgt[tpos] = 0;
	return tpos;
}

/*
 * checks if pat has a matching in text. If text is shorter than pat then
 * the test fails by definition.
 */
bool patMatch(const char *text, const char *pat) {
	char *lotext;
	char *lopat;
	int32_t best = 0;
	int32_t res;
	int32_t plen = strlen(pat);
	int32_t tlen = strlen(text);
	int32_t i, j;

	/* The pattern must not be longer than the text! */
	if (tlen < plen) {
		return false;
	}

	/* A pattern must deliver at least some information */
	if (strlen(pat) < 2) {
		return false;
	}

	/* prepare the pattern */
	lopat = (char *) falloc(strlen(pat) + 3, 1);
	patPrep(lopat + 1, pat, plen);
	lopat[0] = 0;
	lopat[plen + 1] = 0;

	/* prepare the text */
	lotext = (char *) falloc(tlen + 1, 1);
	patPrep(lotext, text, tlen);

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

	/* compute percentual match */
	res = (100 * best) / plen;
	sfree(&lotext);
	sfree(&lopat);
	return (res >= SIMGUARD);
}

/*
 * symmetric patMatch that checks if text is shorter than pattern
 * used for artist checking in addNewTitle
 */
bool checkSim(const char *text, const char *pat) {
	if (strlen(text) < strlen(pat)) {
		return patMatch(pat, text);
	}
	else {
		return patMatch(text, pat);
	}
}

/*
 * like strncpy but len is the max len of the target string, not the number of
 * bytes to copy.
 * Also the target string is always terminated with a 0 byte
 */
size_t strtcpy(char *t, const char *s, size_t l) {
	strncpy(t, s, l);
	t[l - 1] = 0;
	return strlen(t);
}

/*
 * like strncat but len is the max len of the target string (w/o NUL), not the
 * number of bytes to copy.
 */
size_t strtcat(char *t, const char *s, size_t l) {
	size_t start = strlen(t);
	size_t end = start + strlen(s);
	size_t pos;

	if (end > l)
		end = l;

	for (pos = start; pos < end; pos++) {
		t[pos] = s[pos - start];
	}
	t[end] = 0;

	return strlen(t);
}

/**
 * turn a relative path into an absolute path
 * basedir is the current directory, which does not necessarily need to
 * be the real current directory and may be used to maintain config and data
 * directory structures
 */
char *abspath(char *path, const char *basedir, const size_t len) {
	char *buff;

	if (path[0] != '/') {
		buff = (char *) falloc(len + 2, 1);
		snprintf(buff, len, "%s/%s", basedir, path);
		strtcpy(path, buff, len);
		free(buff);
	}

	return path;
}

/**
 * Strip spaces and special chars from the beginning and the end
 * of 'text', truncate it to 'maxlen' to keep space for the terminating null
 * and store the result in 'buff'. buff MUST have a size of at least maxlen+1!
 *
 * @todo: take proper care of wide characters!
**/
char *strip(char *buff, const char *text, const size_t maxlen) {
	int32_t len = strlen(text);
	int32_t tpos = 0;

	/* clear target buffer */
	memset(buff, 0, maxlen + 1);

	if (len == 0) {
		return buff;
	}

	/* Cut off leading spaces and special chars */
	while ((tpos < len) && ((uint8_t) text[tpos] <= 32)) {
		tpos++;
	}

	len = MIN(strlen(text + tpos), maxlen);
	strtcpy(buff, text + tpos, len + 1);

	/* Cut off trailing spaces and special chars */
	while ((len > 0) && ((uint8_t) text[tpos + len] <= 32)) {
		buff[len] = 0;
		len--;
	}

	return buff;
}

/**
 * simple in-place strip
 **/
char *instrip(char *string) {
	if (string == NULL) {
		return NULL;
	}
	char *buf = strdup(string);

	strip(string, buf, strlen(string));
	free(buf);
	return string;
}

/* returns a line of text from the FILE
 * The line is read until EOF or \n and then returned. The returned string
 * must be free()d. If no data is available the function returns NULL */
char *fetchline(FILE * fp) {
	char *line = NULL;
	int32_t len = 0;
	int32_t size = 0;
	int32_t c;

	c = fgetc(fp);
	while ((c != EOF) && (c != (int32_t) '\n')) {
		if (len >= size - 2) {
			size = size + 256;
			line = (char *) frealloc(line, size);
		}
		line[len++] = (char) c;
		line[len] = 0;
		c = fgetc(fp);
	}

	return line;
}

/**
 * reads from the fd into the line buffer until either a CR
 * comes or the fd stops sending characters.
 * returns number of read bytes, -1 on overflow or 0 if
 * no data was sent. This means that reading a line containing 
 * of a carriage return will return 1 (as one character was read)
 */
size_t readline(char *line, size_t len, int fd) {
	size_t cnt = 0;
	char c;

	while (0 != read(fd, &c, 1)) {
		if (cnt < len) {
			if ('\n' || '\r' == c) {
				c = 0;
			}

			line[cnt] = c;
			cnt++;

			if (0 == c) {
				return cnt;
			}
		}
		else {
			return (size_t) -1;
		}
	}

	/* avoid returning unterminated strings. */
	/* this code should never be reached but maybe there is */
	/* a read() somewhere that timeouts.. */
	line[cnt] = 0;
	if (cnt > 0)
		cnt++;

	return cnt;
}

/**
 * checks if text ends with suffix
 * this function is case insensitive
 */
bool endsWith(const char *text, const char *suffix) {
	int32_t i, tlen, slen;

	tlen = strlen(text);
	slen = strlen(suffix);

	if (tlen < slen) {
		return false;
	}

	for (i = slen; i > 0; i--) {
		if (tolower(text[tlen - i]) != tolower(suffix[slen - i])) {
			return false;
		}
	}

	return true;
}

/**
 * checks if text starts with prefix
 * this function is case insensitive
 */
bool startsWith(const char *text, const char *prefix) {
	int32_t i, tlen, plen;

	tlen = strlen(text);
	plen = strlen(prefix);

	if (tlen < plen) {
		return 0;
	}

	for (i = 0; i < plen; i++) {
		if (tolower(text[i]) != tolower(prefix[i])) {
			return false;
		}
	}

	return true;
}

/**
 * Check if a file is a music file
 */
bool isMusic(const char *name) {
	return endsWith(name, ".mp3");
	/*
	 * if( endsWith( name, ".mp3" ) || endsWith( name, ".ogg" ) ) return 1;
	 * return 0;
	 */
}


/**
 * Check if the given string is an URL
 * We just allow http/s and no parameters
 */
bool isURL(const char *uri) {
	if (!startsWith(uri, "http://") && !startsWith(uri, "https://")) {
		return false;
	}
	if (strchr(uri, '?') != NULL) {
		return false;
	}
	if (strchr(uri, '&') != NULL) {
		return false;
	}

	return true;
}

/*
 * Inplace conversion of a string to lowercase
 */
char *toLower(char *text) {
	size_t i;

	for (i = 0; i < strlen(text); i++) {
		text[i] = tolower(text[i]);
	}

	return text;
}

/**
 * works like strtcpy but turns every character to lowercase
 */
int32_t strltcpy(char *dest, const char *src, const size_t len) {
	strtcpy(dest, src, len);
	dest = toLower(dest);
	return strlen(dest);
}

/**
 * works like strtcat but turns every character to lowercase
 */
int32_t strltcat(char *dest, const char *src, const size_t len) {
	strtcat(dest, src, len);
	dest = toLower(dest);
	return strlen(dest);
}

/**
 * checks if the given path is an accessible directory
 */
bool isDir(const char *path) {
	struct stat st;

	return (!stat(path, &st) && S_ISDIR(st.st_mode));
}

/**
 * wrapper around calloc that fails in-place with an error
 */
void *falloc(size_t num, size_t size) {
	void *result = NULL;

	result = calloc(num, size);

	if (NULL == result) {
		abort();
	}

	return result;
}

/**
 * wrapper around realloc that fails in-place with an error
 */
void *frealloc(void *old, size_t size) {
	void *newval = NULL;

	newval = realloc(old, size);
	if (newval == NULL) {
		abort();
	}
	return newval;
}

/**
 * just free something if it actually exists and set the pointer to NULL
 */
void sfree(char **ptr) {
	if (*ptr != NULL) {
		free(*ptr);
	}
	*ptr = NULL;
}

/*
 * debug function to dump binary data on the screen
 */
void dumpbin(const void *data, size_t len) {
	size_t i, j;

	for (j = 0; j <= len / 8; j++) {
		for (i = 0; i < 8; i++) {
			if (i <= len) {
				printf("%02x ", ((char *) data)[(j * 8) + i]);
			}
			else {
				printf("-- ");
			}
		}
		putchar(' ');
		for (i = 0; i < 8; i++) {
			if (((j * 8) + i <= len) && isprint(((char *) data)[(j * 8) + i])) {
				putchar(((char *) data)[(j * 8) + i]);
			}
			else {
				putchar('.');
			}
		}
		putchar('\n');
	}
}

/**
 * treats a single character as a hex value
 */
int32_t hexval(const char c) {
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}

	if ((c >= 'a') && (c <= 'f')) {
		return 10 + (c - 'a');
	}

	if ((c >= 'A') && (c <= 'F')) {
		return 10 + (c - 'A');
	}

	return -1;
}

/**
 * wrapper for the standard write() which handles partial writes and allows
 * ignoring the return value
 */
int32_t dowrite(const int32_t fd, const char *buf, const size_t buflen) {
	const char *pos = buf;
	size_t sent = 0;
	ssize_t ret = 0;

	while (sent < buflen) {
		ret = write(fd, pos + sent, buflen - sent);
		if (ret == -1) {
			return -1;
		}
		sent = sent + ret;
	}
	return sent;
}

/**
 * move the current file file into a backup
 */
int32_t fileBackup(const char *name) {
	char backupname[MAXPATHLEN + 1] = "";

	strtcpy(backupname, name, MAXPATHLEN);
	strtcat(backupname, ".bak", MAXPATHLEN);

	if (rename(name, backupname)) {
		return errno;
	}

	return 0;
}

/**
 * move the backup file file back to the current file
 */
int32_t fileRevert(const char *path) {
	char backup[MAXPATHLEN + 1];

	strtcpy(backup, path, MAXPATHLEN);
	strtcat(backup, ".bak", MAXPATHLEN);

	if (rename(backup, path)) {
		return errno;
	}

	return 0;
}

/* waits for a keypress
   timeout - timeout in ms
   returns character code of keypress or -1 on timeout */
int32_t getch(long timeout) {
	int32_t c = 0;
	struct termios org_opts, new_opts;
	struct pollfd pfd;

	/* get current settings if this fails the terminal is not fully featured */
	assert(tcgetattr(STDIN_FILENO, &org_opts) == 0);

	memcpy(&new_opts, &org_opts, sizeof (new_opts));
	new_opts.c_lflag &=
		~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
	tcsetattr(STDIN_FILENO, TCSANOW, &new_opts);

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;

	if (poll(&pfd, 1, timeout) > 0) {
		c = getchar();
	}
	else {
		c = -1;
	}

	assert(tcsetattr(STDIN_FILENO, TCSANOW, &org_opts) == 0);
	return c;
}

/*
 * waits for a keyboard event
 * returns:
 * -1 - timeout
 *  0 - no key
 *  1 - key
 *
 * code will be set to the scancode of the key on release and to the
 * negative scancode on repeat.
 */
int32_t getEventCode(int32_t * code, int32_t fd, uint32_t timeout,
					 int32_t repeat) {
	int32_t emergency = 0;
	struct pollfd pfd;
	struct input_event ie;
	int pr;

	*code = 0;

	/* just return timeouts and proper events.
	 * Claim timeout a fter 1000 non-kbd events in a row */
	while (emergency++ < 1000) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		pr = poll(&pfd, 1, timeout);
		if (pr == 0)
			return -1;
		if ((pr == -1) && (errno != EINTR)) {
			fail(errno, "poll() failed!");
			return -1;
		}
		if (pfd.revents & POLLIN) {
			/* truncated event, this should never happen */
			if (read(fd, &ie, sizeof (ie)) != sizeof (ie)) {
				return -2;
			}
			/* proper event */
			if (ie.type == EV_KEY) {
				/* keypress */
				if (ie.value == 1) {
					*code = ie.code;
					return 1;
				}
				/* repeat */
				if (repeat && (ie.value == 2)) {
					*code = -ie.code;
					return 1;
				}
			}
		}
	}
	return -1;
}
