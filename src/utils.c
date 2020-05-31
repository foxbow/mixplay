/**
 * collection of all-purpose utility functions
 */
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <termios.h>
#include <assert.h>
#include <linux/input.h>

#include "utils.h"

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
	while( ( tpos < len ) && ( (unsigned char)text[tpos] < 32 ) ) {
		tpos++;
	}

	len=MIN(strlen(text+tpos),maxlen);
	strtcpy( buff, text+tpos, len+1 );

	/* Cut off trailing spaces and special chars */
	while( ( len > 0 ) && ( (unsigned char)text[tpos] < 32 ) ) {
		buff[len]=0;
		len--;
	}

	return buff;
}

/* returns a line of text from the FILE
 * The line is read until EOF or \n and then returned. The returned string
 * must be free()d. If no data is available the function returns NULL */
char *fetchline( FILE *fp ) {
	char *line=falloc( 256, 1 );
	char *rv=NULL;
	int len=0;
	int size=255;
	int c;

	c=fgetc(fp);
	while( (c != EOF) && (c != (int)'\n' ) ) {
		if( len == size ) {
			size=size+256;
			line=frealloc(line, size);
		}
		line[len++]=(char)c;
		line[len]=0;
		c=fgetc(fp);
	}

	if( len > 0 ) {
		rv=strdup(line);
	}
	free(line);

	return rv;
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

	return 1;
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

	return 1;
}

/**
 * Check if a file is a music file
 */
int isMusic( const char *name ) {
	return endsWith( name, ".mp3" );
	/*
	if( endsWith( name, ".mp3" ) || endsWith( name, ".ogg" ) ) return 1;
	return 0;
	*/
}


/**
 * Check if the given string is an URL
 * We just allow http/s
 */
int isURL( const char *uri ) {
	if( startsWith( uri, "http://" ) || startsWith( uri, "https://" ) ) {
		return 1;
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
		return 1;
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
 * debug function to dump binary data on the screen
 */
void dumpbin( const void *data, size_t len ) {
	size_t i, j;
	for( j=0; j<=len/8; j++ ) {
		for( i=0; i< 8; i++ ) {
			if( i <= len ) {
				printf("%02x ", ((char*)data)[(j*8)+i] );
			}
			else {
				printf("-- ");
			}
		}
		putchar(' ');
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
	if( ( c >= '0' ) && ( c <= '9' ) ) {
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

/**
 * wrapper for the standard write() which handles partial writes and allows
 * ignoring the return value
 */
int dowrite( const int fd, const char *buf, const size_t buflen ) {
	const char *pos=buf;
	size_t sent=0;
	ssize_t ret=0;

	while( sent < buflen ) {
		ret=write( fd, pos+sent, buflen-sent );
		if( ret == -1 ) {
			return -1;
		}
		sent=sent+ret;
	}
	return sent;
}

/**
 * move the current file file into a backup
 */
int fileBackup( const char *name ) {
	char backupname[MAXPATHLEN+1]="";

	strtcpy( backupname, name, MAXPATHLEN );
	strtcat( backupname, ".bak", MAXPATHLEN );

	if( rename( name, backupname ) ) {
		return errno;
	}

	return 0;
}

/**
 * move the backup file file back to the current file
 */
int fileRevert( const char *path ) {
	char backup[MAXPATHLEN+1];

	strtcpy(backup, path, MAXPATHLEN);
	strtcat(backup, ".bak", MAXPATHLEN );

	if( rename( backup, path ) ) {
		return errno;
	}

	return 0;
}

/* waits for a keypress
   timeout - timeout in ms
	 returns character code of keypress or -1 on timeout */
int getch( long timeout ) {
	int c=0;
	struct termios org_opts, new_opts;
	fd_set fds;
	struct timeval to;

	/* get current settings if this fails the terminal is not fully featured */
	assert( tcgetattr(STDIN_FILENO, &org_opts) == 0 );

	memcpy(&new_opts, &org_opts, sizeof(new_opts));
	new_opts.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOPRT | ECHOKE | ICRNL);
	tcsetattr(STDIN_FILENO, TCSANOW, &new_opts);

	FD_ZERO( &fds );
	FD_SET( fileno( stdin ), &fds );

	to.tv_sec=(timeout/1000);
	to.tv_usec=(timeout-(to.tv_sec * 1000))*1000;
	select( FD_SETSIZE, &fds, NULL, NULL, &to );

	if( FD_ISSET( fileno( stdin ), &fds ) ) {
		c=getchar();
	}
	else {
		c=-1;
	}

	assert( tcsetattr(STDIN_FILENO, TCSANOW, &org_opts) == 0 );
	return c;
}

/*
 * waits for a keyboard event
 * returns:
 * -1 - timeout
 *  0 - no key
 *  1 - key
 *
 * code will be set to the scancode of the key on release and to the
 * negative scancode on repeat.
 */
int getEventCode( int *code, int fd, unsigned timeout, int repeat ) {
	fd_set fds;
	struct timeval to;
	struct input_event ie;
	int emergency=0;
	*code = 0;

	/* just return timeouts and proper events.
	   Claim timeout after 1000 non-kbd events in a row */
	while( emergency < 1000 ) {
		FD_ZERO( &fds );
		FD_SET( fd, &fds );
		to.tv_sec=(timeout/1000);
		to.tv_usec=(timeout-(to.tv_sec * 1000))*1000;

		select( FD_SETSIZE, &fds, NULL, NULL, &to );
		/* timeout */
		if( !FD_ISSET( fd, &fds ) ) {
			return -1;
		}
		/* truncated event, this should never happen */
		if( read( fd, &ie, sizeof(ie) ) != sizeof(ie) ){
			return -2;
		}
		/* proper event */
		if( ie.type == EV_KEY ) {
			/* keypress */
			if ( ie.value == 1 ) {
				*code = ie.code;
				return 1;
			}
			/* repeat */
			if( repeat && ( ie.value == 2 ) ) {
				*code = -ie.code;
				return 1;
			}
		}
		emergency++;
	}
	return -1;
}
