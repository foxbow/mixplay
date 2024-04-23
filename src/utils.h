#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

/* Default values */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define F_FAIL -1

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#define MAXPATHLEN 256

/* similarity index for checksim */
#define SIMGUARD 70

/*
 * string helper functions that avoid target buffer overflows
 */
size_t strtcpy(char *t, const char *s, size_t l);
size_t strtcat(char *t, const char *s, size_t l);

/*
 * These functions need to be implemented in the UI
 */
void fail(const int32_t error, const char *msg, ...)
	__attribute__ ((__format__(__printf__, 2, 3))) __attribute__ ((noreturn));

/**
 * General utility functions
 */
bool patMatch(const char *text, const char *pat);
bool checkSim(const char *text, const char *pat);
int32_t strltcpy(char *dest, const char *src, const size_t len);
int32_t strltcat(char *dest, const char *src, const size_t len);
char *strip(char *dest, const char *src, const size_t len);
char *instrip(char *txt);
bool endsWith(const char *text, const char *suffix);
bool startsWith(const char *text, const char *prefix);
bool isURL(const char *uri);
bool isDir(const char *path);
char *fetchline(FILE * fp);
int32_t readline(char *line, size_t len, int32_t fd);
char *abspath(char *path, const char *basedir, const size_t len);
void *falloc(size_t num, size_t size);
void *frealloc(void *old, size_t size);
void sfree(char **ptr);
void dumpbin(const void *data, size_t len);
char *toLower(char *text);
int32_t hexval(const char c);
int32_t dowrite(const int32_t fd, const char *buf, const size_t buflen);
int32_t fileBackup(const char *name);
int32_t fileRevert(const char *path);
int32_t getch(long timeout);
int32_t getEventCode(int32_t * code, int32_t fd, uint32_t timeout,
					 int32_t repeat);
#endif
