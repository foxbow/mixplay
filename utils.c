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
        buff=falloc( sizeof( char ),len );
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
static char *toLower( char *text ) {
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
 * wrapper around calloc that fails in-place with an error
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
 * add a line to a list of lines
 * if the result is longer than the length of the buffer,
 * remove lines from the start until it fits again
 */
int scrollAdd( char *scroll, const char* line, const size_t len ) {
    char *pos;

    if( strlen( line ) > len ) {
        fail( F_FAIL, "Adding too much text to scroll!" );
    }

    if( ( strlen( scroll ) + strlen( line ) ) < len ) {
        strncat( scroll, line, len );
        return 0;
    }

    while( ( strlen( scroll ) + strlen( line ) ) >= len ) {
        pos=strchr( scroll, '\n' );

        if( NULL==pos ) {
            fail( F_FAIL, "Scroll has one line with the length of %i?", len );
        }

        pos++;
        memmove( scroll, pos, strlen( pos ) );
    }

    strncat( scroll, line, len );
    return 1;
}

/**
 * helperfunction to implement message ringbuffer
 */
void msgBuffAdd( struct msgbuf_t msgbuf, char *line ) {
	if( msgbuf.lines == MSGNUM ) {
		free(msgbuf.msg[msgbuf.current]);
		msgbuf.msg[msgbuf.current]=line;
		msgbuf.current=(msgbuf.current+1)%MSGNUM;
	}
	else {
		msgbuf.msg[(msgbuf.current+msgbuf.lines)%MSGNUM]=line;
		msgbuf.lines++;
	}
}

/**
 * helperfunction to implement message ringbuffer
 */
char *msgBuffGet( struct msgbuf_t msgbuf ) {
	char *retval = NULL;
	if( msgbuf.lines > 0 ) {
		retval=msgbuf.msg[msgbuf.current];
		msgbuf.msg[msgbuf.current]=NULL;
		msgbuf.current = (msgbuf.current+1)%MSGNUM;
		msgbuf.lines--;
	}
	return retval;
}

/**
 * returns all lines in the buffer as a single string
 * Does not empty the buffer
 */
char *msgBuffPeek( struct msgbuf_t msgbuf ) {
	int i;
	size_t len=200;
	char *buff;

	buff=falloc( len, sizeof( char ) );
	buff[0]=0;

	for( i=0; i<msgbuf.lines; i++ ) {
		if( strlen(buff)+strlen(msgbuf.msg[(i+msgbuf.current)%MSGNUM]) >= 1024 ) {
			len=len+200;
			buff=realloc( buff, len*sizeof( char ) );
			if( NULL == buff ) {
				fail( errno, "Can't increase message buffer " );
			}
		}
		strcat( buff, msgbuf.msg[(i+msgbuf.current)%MSGNUM] );
		strcat( buff, "\n" );
	}

	return buff;
}

/**
 * empties the mesage buffer
 */
void msgBuffClear( struct msgbuf_t msgbuf ) {
	char *line;
	while( ( line=msgBuffGet( msgbuf ) ) != NULL ) {
		free( line );
	}
}

/**
 * better strncat(), this takes the original string length into account and makes sure that
 * the result is terminated properly.
 * Returns the concatenated string on success and NULL on overflow
 * On overflow line will be truncated to maxlen but still be usable.
 */
char *appendString( char *line, const char *val, const size_t maxlen ){
	int i, j=0, l;
	char *retval=line;

	j=strlen(line);
	l=j+strlen( val );
	if( l > maxlen-1 ) {
		l=maxlen-1;
		retval=0;
	}

	for( i=0; i<l; i++ ) {
		line[i]=val[j];
		j++;
	}

	return retval;
}

/**
 * line appendString() but for an integer value.
 */
char *appendInt( char *line, const char *fmt, const int val, size_t maxlen ) {
	static char numbuff[256];
	snprintf( numbuff, 255, fmt, val );
	return appendString( line, numbuff, maxlen );
}
