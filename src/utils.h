#ifndef FBFS_UTILS_H
#define FBFS_UTILS_H

/* Default values */
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#define ONOFF(x) (x)?"ON":"OFF"

#define F_FAIL -1

#ifndef MAX
#define MAX(x,y) ((x)>(y)?(x):(y))
#endif
#ifndef MIN
#define MIN(x,y) ((x)<(y)?(x):(y))
#endif

#define MSGNUM 20

/*
 * Message ringbuffer structure
 */
struct msgbuf_t {
	char *msg[MSGNUM];			/* the message buffer */
	int  current;				/* index if the first unhandles message line */
	int  lines;					/* how many message lines are in use */
	unsigned long count;		/* the number of the last read message */
	pthread_mutex_t *msgLock;	/* mutex to control access to the messages */
};
typedef struct msgbuf_t msgbuf;

#include "config.h"

/*
 * string helper functions that avoid target buffer overflows
 */
size_t strtcpy( char *t,const  char *s, size_t l );
size_t strtcat( char *t,const  char *s, size_t l );

/*
 * These functions need to be implemented in the UI
 * See: ncbox.c, gladeutils.c
 */
void fail( const int error, const char* msg, ... ) __attribute__ ((noreturn));
void activity( const char *msg, ... );

/**
 * helperfunction to implement message ringbuffer
 */
msgbuf *msgBuffInit();
unsigned long  msgBuffAdd( msgbuf *msgbuf, char *line );
char *msgBuffGet( msgbuf *msgbuf );
const char *msgBuffPeek( msgbuf *msgbuf, unsigned long msgno );
char *msgBuffAll(  msgbuf *msgbuf );
void  msgBuffClear( msgbuf *msgbuf );
void msgBuffDiscard( struct msgbuf_t *msgbuf );

/**
 * General utility functions
 */
void setTitle( const char* title );
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
long readHex( char *txt, char **end );

void blockSigint();
#endif
