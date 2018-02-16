#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

/* Default values */
#include <stdlib.h>
#include <pthread.h>

#define ONOFF(x) (x)?"ON":"OFF"

#define CMP_CHARS 127
#define CMP_BITS (CMP_CHARS*CMP_CHARS)
#define CMP_ARRAYLEN ((CMP_BITS%8==0)?CMP_BITS/8:(CMP_BITS/8)+1)

#define F_FAIL -1

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif
/* Represents a string as a bit array */
typedef unsigned char* strval_t;

#define MSGNUM 20

/*
 * Message ringbuffer structure
 */
struct msgbuf_t {
	char *msg[MSGNUM];
	int  current;
	int  lines;
	unsigned long count;
	pthread_mutex_t *msgLock;
};
typedef struct msgbuf_t msgbuf;

#include "config.h"

/*
 * These functions need to be implemented in the UI
 * See: ncbox.c, gladeutils.c
 */
void fail( int error, const char* msg, ... );
void activity( const char *msg, ... );
void progressStart( char *msg, ... );
void progressEnd( void );
void updateUI( void );

/**
 * helperfunction to implement message ringbuffer
 */
msgbuf *msgBuffInit();
unsigned long  msgBuffAdd( msgbuf *msgbuf, char *line );
char *msgBuffGet( msgbuf *msgbuf );
const char *msgBuffPeek( msgbuf *msgbuf, unsigned long msgno );
char *msgBuffAll(  msgbuf *msgbuf );
void  msgBuffClear( msgbuf *msgbuf );

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
void *frealloc( void *old, size_t size );
void sfree( char **ptr );
void dumpbin( const void *data, size_t len );
char *toLower( char *text );

#endif
