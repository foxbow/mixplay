/**
 * collection of all-purpose utility functions
 */

#include "utils.h"
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/stat.h>
#include <signal.h>
#include <pthread.h>

void blockSigint() {
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, SIGINT);
	if( pthread_sigmask(SIG_BLOCK, &set, NULL) != 0 ) {
		addMessage( 0, "Could not block SIGINT!" );
	}
}
/*
 * like strncpy but len is the max len of the target string, not the number of bytes to copy.
 * Also the target string is always terminated with a 0 byte
 */
size_t strtcpy( char *t,const  char *s, size_t l ) {
	strncpy( t, s, l );
	t[l-1]=0;
	return strlen(t);
}

/*
 * like strncat but len is the max len of the target string, not the number of bytes to copy.
 */
size_t strtcat( char *t, const char *s, size_t l ) {
	l=MIN( strlen(t)+strlen(s), l-1 )-strlen(t);
	if( l > 0 ) {
		strncat( t, s, l );
	}
	return strlen(t);
}

/**
 * turn a relative path into an absolute path
 * basedir is the current directory, which does not necessarily need to
 * be the real current directory and may be used to maintain config and data
 * directory structures
 */
char *abspath( char *path, const char *basedir, const size_t len ) {
	char *buff;

	if( path[0] != '/' ) {
		buff=(char *)falloc( len+2, 1 );
		snprintf( buff, len, "%s/%s", basedir, path );
		strtcpy( path, buff, len );
		free( buff );
	}

	return path;
}

/**
 * Some ANSI code magic to set the terminal title
 **/
void setTitle( const char* title ) {
	char buff[128];
	strtcpy( buff, "\033]2;", 128 );
	strtcat( buff, title, 128 );
	strtcat( buff, "\007\000", 128 );
	fputs( buff, stdout );
	fflush( stdout );
}

/**
 * Strip spaces and special chars from the beginning and the end
 * of 'text', truncate it to 'maxlen' to keep space for the terminating null
 * and store the result in 'buff'. buff MUST have a size of at least maxlen+1!
 *
 * @todo: take proper care of wide characters!
**/
char *strip( char *buff, const char *text, const size_t maxlen ) {
	int len=strlen( text );
	int tpos=0;

	/* clear target buffer */
	memset( buff, 0, maxlen+1 );

	if( len == 0 ) {
		return buff;
	}

	/* Cut off leading spaces and special chars */
	while( ( tpos < len ) && ( isspace( text[tpos] ) ) ) {
		tpos++;
	}

	len=MIN(strlen(text+tpos),maxlen);
	strtcpy( buff, text+tpos, len+1 );

	/* Cut off trailing spaces and special chars */
	while( ( len > 0 ) && ( iscntrl( buff[len] ) || isspace( buff[len] ) ) ) {
		buff[len]=0;
		len--;
	}

	return buff;
}

/**
 * reads from the fd into the line buffer until either a CR
 * comes or the fd stops sending characters.
 * returns number of read bytes or -1 on overflow.
 */
int readline( char *line, size_t len, int fd ) {
	size_t cnt=0;
	char c;

	while ( 0 != read( fd, &c, 1 ) ) {
		if( cnt < len ) {
			if( '\n' == c ) {
				c=0;
			}

			line[cnt]=c;
			cnt++;

			if( 0 == c ) {
				return cnt;
			}
		}
		else {
			return -1;
		}
	}

	/* avoid returning unterminated strings. */
	/* this code should never be reached but maybe there is */
	/* a read() somewhere that timeouts.. */
	line[cnt]=0;
	cnt++;

	return cnt;
}

/**
 * checks if text ends with suffix
 * this function is case insensitive
 */
int endsWith( const char *text, const char *suffix ) {
	int i, tlen, slen;
	tlen=strlen( text );
	slen=strlen( suffix );

	if( tlen < slen ) {
		return 0;
	}

	for( i=slen; i>0; i-- ) {
		if( tolower( text[tlen-i] ) != tolower( suffix[slen-i] ) ) {
			return 0;
		}
	}

	return -1;
}

/**
 * checks if text starts with prefix
 * this function is case insensitive
 */
int startsWith( const char *text, const char *prefix ) {
	int i, tlen, plen;
	tlen=strlen( text );
	plen=strlen( prefix );

	if( tlen < plen ) {
		return 0;
	}

	for( i=0; i<plen; i++ ) {
		if( tolower( text[i] ) != tolower( prefix[i] ) ) {
			return 0;
		}
	}

	return -1;
}

/**
 * Check if a file is a music file
 */
int isMusic( const char *name ) {
	return endsWith( name, ".mp3" );
	/*
	if( endsWith( name, ".mp3" ) || endsWith( name, ".ogg" ) ) return -1;
	return 0;
	*/
}


/**
 * Check if the given string is an URL
 * We just allow http/s
 */
int isURL( const char *uri ) {
	if( startsWith( uri, "http://" ) || startsWith( uri, "https://" ) ) {
		return -1;
	}

	return 0;
}

/*
 * Inplace conversion of a string to lowercase
 */
char *toLower( char *text ) {
	size_t i;

	for( i=0; i<strlen( text ); i++ ) {
		text[i]=tolower( text[i] );
	}

	return text;
}

/**
 * works like strtcpy but turns every character to lowercase
 */
int strltcpy( char *dest, const char *src, const size_t len ) {
	strtcpy( dest, src, len );
	dest=toLower( dest );
	return strlen( dest );
}

/**
 * works like strncat but turns every character to lowercase
 */
int strltcat( char *dest, const char *src, const size_t len ) {
	strtcat( dest, src, len );
	dest=toLower( dest );
	return strlen( dest );
}

/**
 * checks if the given path is an accessible directory
 */
int isDir( const char *path ) {
	struct stat st;

	if( !stat( path, &st ) && S_ISDIR( st.st_mode ) ) {
		return -1;
	}

	return 0;
}

/**
 * wrapper around malloc/calloc that fails in-place with an error
 */
void *falloc( size_t num, size_t size ) {
	void *result=NULL;

	result=calloc( num, size );

	if( NULL == result ) {
		addMessage( 0,"Sorry, can't falloc (%i)!", errno );
		abort();
	}

	return result;
}

/**
 * wrapper around realloc that fails in-place with an error
 */
void *frealloc( void *old, size_t size ) {
	void *newval=NULL;
	newval=realloc( old, size );
	if( newval == NULL ) {
		addMessage( 0, "Sorry, can't realloc (%i)!", errno );
		abort();
	}
	return newval;
}

/**
 * just free something if it actually exists
 */
void sfree( char **ptr ) {
	if( *ptr != NULL ) {
		free( *ptr );
	}
	*ptr=NULL;
}

/*
 * initialize a message ringbuffer
 */
msgbuf *msgBuffInit() {
	msgbuf *msgBuf=(msgbuf *)falloc( 1, sizeof( msgbuf ) );
	msgBuf->msgLock=(pthread_mutex_t *)falloc( 1, sizeof( pthread_mutex_t ) );
	msgBuf->lines=0;
	msgBuf->current=0;
	msgBuf->count=0;
	pthread_mutex_init( msgBuf->msgLock, NULL );
	return msgBuf;
}

/**
 * helperfunction to implement message ringbuffer
 * adds message 'line' to the buffer.
 * returns the current message number
 */
unsigned long msgBuffAdd( msgbuf *msgbuf, char *line ) {
	char *myline;
	myline=(char*)falloc( strlen(line)+1, 1 );
	strcpy( myline, line );
	pthread_mutex_lock( msgbuf->msgLock );
	/* overflow? */
	if( msgbuf->lines == MSGNUM ) {
		/* discard oldest (current) message */
		free(msgbuf->msg[msgbuf->current]);
		/* replace with new message */
		msgbuf->msg[msgbuf->current]=myline;
		/* bump current message to the next oldest */
		msgbuf->current=(msgbuf->current+1)%MSGNUM;
	}
	else {
		/* current+lines points to the next free buffer */
		msgbuf->msg[(msgbuf->current+msgbuf->lines)%MSGNUM]=myline;
		msgbuf->lines++;
	}
	msgbuf->count++;
	pthread_mutex_unlock( msgbuf->msgLock );
	return msgbuf->count;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and removes it from the buffer
 * Return pointer must be free'd after use!
 */
char *msgBuffGet( struct msgbuf_t *msgbuf ) {
	char *retval = NULL;
	pthread_mutex_lock( msgbuf->msgLock );
	if( msgbuf->lines > 0 ) {
		retval=msgbuf->msg[msgbuf->current];
		msgbuf->msg[msgbuf->current]=NULL;
		msgbuf->current =(msgbuf->current+1)%MSGNUM;
		msgbuf->lines--;
	}
	pthread_mutex_unlock( msgbuf->msgLock );
	return retval;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and keeps it in the buffer
 * Return pointer MUST NOT be free'd after use!
 * Caveat: Returns "" if no messages are available
 */
const char *msgBuffPeek( struct msgbuf_t *msgbuf, unsigned long msgno ) {
	const char *retval = "";
	int pos;

	pthread_mutex_lock( msgbuf->msgLock );
	if( msgno < msgbuf->count ) {
		/* Avoid Underflows!*/
		if( msgno >= msgbuf->count-msgbuf->lines ) {
			pos=msgbuf->current+msgbuf->lines; /* the latest entry */
			pos=pos-(msgbuf->count-msgno ); /* get the proper offset */
			retval=msgbuf->msg[pos%MSGNUM];
		}
	}
	pthread_mutex_unlock( msgbuf->msgLock );

	return retval;
}

/**
 * returns all lines in the buffer as a single string
 * Does not empty the buffer
 * Return pointer SHOULD be free'd after use!
 * Caveat: Returns NULL if no messages are available
 */
char *msgBuffAll( struct msgbuf_t *msgbuf ) {
	int i, lineno;
	char *buff;
	size_t len=256;

	buff=(char*)falloc( len, 1 );
	buff[0]=0;

	pthread_mutex_lock( msgbuf->msgLock );
	for( i=0; i<msgbuf->lines; i++ ) {
		lineno=(i+msgbuf->current)%MSGNUM;
		while( strlen(buff)+strlen(msgbuf->msg[lineno]) >= len ) {
			len=len+256;
			buff=(char*)frealloc( buff, len );
		}
		strcat( buff, msgbuf->msg[lineno] );
		strcat( buff, "\n" );
	}
	pthread_mutex_unlock( msgbuf->msgLock );

	return buff;
}

/**
 * empties the message buffer
 */
void msgBuffClear( struct msgbuf_t *msgbuf ) {
	char *line;
	while( ( line=msgBuffGet( msgbuf ) ) != NULL ) {
		free( line );
	}
}

/*
 * Discards the message buffer and all contents
 */
void msgBuffDiscard( struct msgbuf_t *msgbuf ) {
	msgBuffClear( msgbuf );
	free( msgbuf->msgLock );
	free( msgbuf );
}

/*
 * debug function to dump binary data on the screen
 */
void dumpbin( const void *data, size_t len ) {
	size_t i, j;
	for( j=0; j<len/8; j++ ) {
		for( i=0; i< 8; i++ ) {
			if( i <= len ) {
				printf("%02x ", ((char*)data)[(j*8)+i] );
			}
			else {
				printf("00 ");
			}
		}
		printf( "	" );
		for( i=0; i< 8; i++ ) {
			if( ( (j*8)+i <= len ) && isprint( ((char*)data)[(j*8)+i] ) ) {
				putchar( ((char*)data)[(j*8)+i] );
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
int hexval( const char c ) {
	if( ( c-'0' >= 0 ) && ( c-'9' <= 9 ) ) {
		return c-'0';
	}

	if( ( c >= 'a') && ( c <= 'f' ) ) {
		return 10+(c-'a');
	}

	if( ( c >= 'A') && ( c <= 'F' ) ) {
		return 10+(c-'A');
	}

	return -1;
}

/*
 * reads a hex number
 * if end != NULL it will point to the first unknown character.
 */
long readHex( char *txt, char **end ) {
	int pos=0;
	long retval=0;

	if( ( txt==NULL ) || ( strlen(txt) == 0 ) ) {
		return -1;
	}

	while( isxdigit(txt[pos]) ) {
		retval*=16;
		retval+=hexval(txt[pos]);
		pos++;
	}

	if( end != NULL ) {
		*end=txt+pos;
	}

	return retval;
}
