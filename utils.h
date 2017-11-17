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
/* Represents a string as a bit array */
typedef unsigned char* strval_t;

/*
 * Verbosity handling of the utils functions
 */
int getVerbosity( void );
int setVerbosity( int );
int incVerbosity();
void muteVerbosity();

/*
 * These functions need to be implemented in the UI
 * See: ncbox.c, gladeutils.c
 */
void printver( int vl, const char *msg, ... );
void fail( int error, const char* msg, ... );
void activity( const char *msg, ... );
void progressStart( const char *msg, ... );
#define progressLog( ... ) printver( 0, __VA_ARGS__ )
/* void progressLog( const char *msg, ... ); */
void progressEnd( const char *msg );
void updateUI( void *data );

/**
 * String manipulation functions to get rid of warnings in strncat,
 * snprintf etc
 */
char *appendString( char *line, const char *val, size_t maxlen );
char *appendInt( char *line, const char *fmt, const int val, size_t maxlen );

/**
 * General utility functions
 */
void setTitle( const char* title );
int strlncpy( char *dest, const char *src, const size_t len );
int strlncat( char *dest, const char *src, const size_t len );
char *strip( char *buff, const char *text, const size_t maxlen );
int endsWith( const char *text, const char *suffix );
int startsWith( const char *text, const char *prefix );
int isURL( const char *uri );
int isDir( const char *path );
int readline( char *line, size_t len, int fd );
char *abspath( char *path, const char *basedir, int len );
int checkMatch( const char* name, const char* pat );
void *falloc( size_t num, size_t size );
int scrollAdd( char *scroll, const char* line, const size_t len );
#endif
