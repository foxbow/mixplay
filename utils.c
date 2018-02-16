/**
 * collection of all-purpose utility functions
 */

#include "utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <ncurses.h>
#include <sys/stat.h>

/**
 * turn a relative path into an absolute path
 * basedir is the current directory, which does not necessarily need to
 * be the real current directory and may be used to maintain config and data
 * directory structures
 */
char *abspath( char *path, const char *basedir, int len ) {
    char *buff;

    if( path[0] != '/' ) {
        buff=falloc( len, sizeof( char ) );
        snprintf( buff, len, "%s/%s", basedir, path );
        strncpy( path, buff, len );
        free( buff );
    }

    return path;
}

/**
 * Some ANSI code magic to set the terminal title
 **/
void setTitle( const char* title ) {
    char buff[128];
    strncpy( buff, "\033]2;", 128 );
    strncat( buff, title, 128 );
    strncat( buff, "\007\000", 128 );
    fputs( buff, stdout );
    fflush( stdout );
}


/**
 * Strip spaces and special chars from the beginning and the end
 * of 'text', truncate it to 'maxlen' and store the result in
 * 'buff'.
 *
 * @todo: take proper care of wide characters! Right now control (and
 *        extended) characters are simply filtered out. But different
 *        encodings WILL cause problems in any case.
**/
char *strip( char *buff, const char *text, const size_t maxlen ) {
    int len=strlen( text );
    int bpos=0, tpos=0;
    /* clear target buffer */
    memset( buff, 0, maxlen );

    /* Cut off leading spaces and special chars */
    while( ( tpos < len ) && ( isspace( text[tpos] ) ) ) {
        tpos++;
    }

    /* Filter out all extended characters */
    while( ( 0 != text[tpos] )  && ( bpos < ( maxlen-1 ) ) ) {
/*		if( isascii( text[tpos]) ) { */
        if( isprint( text[tpos] ) ) {
            buff[bpos]=text[tpos];
            bpos++;
        }

        tpos++;
    }

    /* Make sure string ends with a 0 */
    buff[bpos]=0;
    bpos--;

    /* Cut off trailing spaces and special chars */
    while( ( bpos > 0 ) && ( iscntrl( buff[bpos] ) || isspace( buff[bpos] ) ) ) {
        buff[bpos]=0;
        bpos --;
    }

    return buff;
}

/**
 * reads from the fd into the line buffer until either a CR
 * comes or the fd stops sending characters.
 * returns number of read bytes or -1 on overflow.
 */
int readline( char *line, size_t len, int fd ) {
    int cnt=0;
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
    int i;

    for( i=0; i<strlen( text ); i++ ) {
        text[i]=tolower( text[i] );
    }

    return text;
}

/**
 * works like strncpy but turns every character to lowercase
 */
int strlncpy( char *dest, const char *src, const size_t len ) {
    strncpy( dest, src, len );
    dest=toLower( dest );
    return strlen( dest );
}

/**
 * works like strncat but turns every character to lowercase
 */
int strlncat( char *dest, const char *src, const size_t len ) {
    strncat( dest, src, len );
    dest=toLower( dest );
    return strlen( dest );
}

/*
 * internally used to set a bit in a long bitlist
 */
static int setBit( unsigned long pos, strval_t val ) {
    int bytepos;
    unsigned char set=0;

    /* avoid under/overflow */
    if( pos < 0 ) {
        pos=0;
    }

    if( pos > CMP_BITS ) {
        pos=CMP_BITS;
    }

    bytepos=pos/8;
    set = 1<<( pos%8 );

    if( 0 == ( val[bytepos] & set ) ) {
        val[bytepos]|=set;
        return 1;
    }

    return 0;
}

/**
 * this creates the raw strval of a string
 * depending on the needed result it may help to turn the strings
 * into lowercase first. For other cases this may be bad so this function
 * acts on the strings as given.
 * If CMP_CHARS is 255 even unicode sequences will be taken into account.
 * This will NOT work across encodings (of course)
 */
static int computestrval( const char* str, strval_t strval ) {
    unsigned char c1, c2;
    int cnt, max=0;

    /* needs at least two characters! */
    if( 2 > strlen( str ) ) {
        return 0;
    }

    for( cnt=0; cnt < strlen( str )-1; cnt++ ) {
        c1=str[cnt]%CMP_CHARS;
        c2=str[cnt+1]%CMP_CHARS;
        max=max+setBit( c1*CMP_CHARS+c2, strval );
    }

    return max;
}

/**
 * internally used to multiplicate two vectors. This is the actual
 * comparison.
 */
static unsigned int vecmult( strval_t val1, strval_t val2 ) {
    unsigned int result=0;
    int cnt;
    unsigned char c;

    for( cnt=0; cnt<CMP_ARRAYLEN; cnt++ ) {
        c=val1[cnt] & val2[cnt];

        while( c != 0 ) {
            if( c &  1 ) {
                result++;
            }

            c=c>>1;
        }
    }

    return result;
}

/**
 * checks if str matches the pattern pat
 * 100 == best match
 **/
static int fncmp( const char *str, const char *pat ) {
    strval_t strval, patval;
    unsigned int maxval;
    long result;
    float step;

    strval=falloc( CMP_ARRAYLEN, sizeof( char ) );
    patval=falloc( CMP_ARRAYLEN, sizeof( char ) );

    maxval=computestrval( pat, patval );
    computestrval( str, strval );

    if( 0 == maxval ) {
        return -1;
    }

    step=100.0/maxval;

    result=vecmult(  strval, patval );

    free( strval );
    free( patval );

    return step*result;
}

/**
 * returns a similarity check of the two strings
 * the trigger index is set automatically by the length of the shortest string
 * returns -1 (true) on match and 0 on mismatch
 */
int checkMatch( const char* name, const char* pat ) {
    int len;
    char loname[1024];
    int trigger;

    strlncpy( loname, name, 1024 );

    len=MIN( strlen( loname ), strlen( pat ) );
    trigger=70;

    if( len <= 20 ) {
        trigger=80;
    }

    if( len <= 10 ) {
        trigger=88;
    }

    if( len <= 5 ) {
        trigger=100;
    }

    if( trigger <= fncmp( loname, pat ) ) {
        return -1;
    }

    return 0;
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
        fail( errno, "Sorry.." );
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
        fail( errno, "Sorry.." );
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

msgbuf *msgBuffInit() {
	msgbuf *msgBuf=falloc( 1, sizeof( msgbuf ) );
	msgBuf->msgLock=falloc( 1, sizeof( pthread_mutex_t ) );
	msgBuf->lines=0;
    msgBuf->current=0;
    msgBuf->count=0;
	pthread_mutex_init( msgBuf->msgLock, NULL );
	return msgBuf;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message number
 */
unsigned long msgBuffAdd( msgbuf *msgbuf, char *line ) {
	char *myline;
	myline=falloc( strlen(line)+1, sizeof( char ) );
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
	if( msgbuf->lines > 0 ) {
		pthread_mutex_lock( msgbuf->msgLock );
		retval=msgbuf->msg[msgbuf->current];
		msgbuf->msg[msgbuf->current]=NULL;
		msgbuf->current =(msgbuf->current+1)%MSGNUM;
		msgbuf->lines--;
		pthread_mutex_unlock( msgbuf->msgLock );
	}
	return retval;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and keeps it in the buffer
 * Return pointer MUST NOT be free'd after use!
 * Caveat: Returns "" if no messages are available
 */
const char *msgBuffPeek( struct msgbuf_t *msgbuf, unsigned long msgno ) {
	char *retval = "";
	int pos;

	if( msgno < msgbuf->count ) {
		pthread_mutex_lock( msgbuf->msgLock );
		pos=msgbuf->current+msgbuf->lines; /* the latest entry */
		pos=pos-(msgbuf->count-msgno ); /* get the proper offset */
		retval=msgbuf->msg[pos%MSGNUM];
		pthread_mutex_unlock( msgbuf->msgLock );
	}
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

	buff=falloc( len, sizeof( char ) );
	buff[0]=0;

	pthread_mutex_lock( msgbuf->msgLock );
	for( i=0; i<msgbuf->lines; i++ ) {
		lineno=(i+msgbuf->current)%MSGNUM;
		while( strlen(buff)+strlen(msgbuf->msg[lineno]) >= len ) {
			len=len+256;
			buff=realloc( buff, len*sizeof( char ) );
			if( NULL == buff ) {
				fail( errno, "Can't increase message buffer" );
			}
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
	int i, j;
	for( j=0; j<len/8; j++ ) {
		for( i=0; i< 8; i++ ) {
			if( i <= len ) {
				printf("%02x ", ((char*)data)[(j*8)+i] );
			}
			else {
				printf("00 ");
			}
		}
		printf( "    " );
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
