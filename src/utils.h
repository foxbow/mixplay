#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

/* Default values */
#include <stdlib.h>
#include <string.h>

#define ONOFF(x) (x)?"ON":"OFF"

#define F_FAIL -1

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

#include "config.h"

/*
 * string helper functions that avoid target buffer overflows
 */
size_t strtcpy( char *t,const  char *s, size_t l );
size_t strtcat( char *t,const  char *s, size_t l );

/*
 * These functions need to be implemented in the UI
 */
void fail( const int error, const char* msg, ... ) __attribute__((__format__(__printf__, 2, 3))) __attribute__ ((noreturn));

/**
 * General utility functions
 */
int strltcpy( char *dest, const char *src, const size_t len );
int strltcat( char *dest, const char *src, const size_t len );
char *strip( char *buff, const char *text, const size_t maxlen );
int endsWith( const char *text, const char *suffix );
int startsWith( const char *text, const char *prefix );
int isURL( const char *uri );
int isDir( const char *path );
int readline( char *line, size_t len, int fd );
char *abspath( char *path, const char *basedir, const size_t len );
void *falloc( size_t num, size_t size );
void *frealloc( void *old, size_t size );
void sfree( char **ptr );
void dumpbin( const void *data, size_t len );
char *toLower( char *text );
int hexval( const char c );
int dowrite( const int fd, const char *buf, const size_t buflen );
int fileBackup( const char *name );
int getch( long timeout );
int getEventCode( int *code, int fd, unsigned timeout, int repeat );
void blockSigint();
#endif
