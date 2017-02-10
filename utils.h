#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

/* Default values */
#include <stdlib.h>
#define ONOFF(x) (x)?"ON":"OFF"

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
// int getVerbosity();
int setVerbosity(int);
int incVerbosity();
void muteVerbosity();

void printver( int vl, const char *msg, ... );

/**
 * General utility functions
 */
void addToFile( const char *path, const char *line );
void setTitle(const char* title);
void fail( int error, const char* msg, ... );
int strlncpy( char *dest, const char *src, const size_t len );
int strlncat( char *dest, const char *src, const size_t len );
char *strip( char *buff, const char *text, const size_t maxlen );
int endsWith( const char *text, const char *suffix );
int startsWith( const char *text, const char *prefix );
int isURL( const char *uri );
int readline( char *line, size_t len, int fd );
void activity( const char *msg, ... );
char *abspath( char *path, char *basedir, int len );
int checkMatch( const char* name, const char* pat ); // should probably replace fncmp()

#endif
