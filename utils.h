#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Default values */

#define CMP_ARRAYLEN 85
#define CMP_BITS 680

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
void addToList( const char *path, const char *line );
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
