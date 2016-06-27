#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <ncurses.h>
#include <unistd.h>

/* Default values */

#define CMP_CHARS 127
#define CMP_BITS (CMP_CHARS*CMP_CHARS)
#define CMP_ARRAYLEN ((CMP_BITS%8==0)?CMP_BITS/8:(CMP_BITS/8)+1)

#define F_WARN 0
#define F_FAIL -1

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif
// Represents a string as a bit array
typedef unsigned char* strval_t;

/*
 * Verbosity handling of the utils functions
 */
int getVerbosity();
int setVerbosity(int);
int incVerbosity();
void muteVerbosity();

/**
 * General utility functions
 */
void addToFile( const char *path, const char *line );
void setTitle(const char* title);
void fail( const char* msg, const char* info, int error );
int fncmp( const char* str1, const char* str2 );
char *toLower( char *text );
char *strip( char *buff, const char *text, const size_t maxlen );
int endsWith( const char *text, const char *suffix );
int startsWith( const char *text, const char *prefix );
int isURL( const char *uri );
int readline( char *line, size_t len, int fd );
void activity( const char *msg );

#endif
