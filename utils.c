#include "utils.h"

static int _ftrpos=0;
static int _ftverbosity=1;

int getVerbosity() {
	return _ftverbosity;
}

int setVerbosity(int v) {
	_ftverbosity=v;
	return _ftverbosity;
}

int incVerbosity() {
	_ftverbosity++;
	return _ftverbosity;
}

void muteVerbosity() {
	_ftverbosity=0;
}

/**
 * turn a relative path into an absolute path
 * basedir is the current directory, which does not necessarily need to
 * be the real current directory and may be used to maintain config and data
 * directory structures
 */
char *abspath( char *path, char *basedir, int len ){
	char *buff;

	if( path[0] != '/' ) {
		buff=calloc(sizeof(char),len);
		if( NULL == buff ) {
			fail( errno, "Can't fix path %s to %s", path, basedir );
		}

		snprintf( buff, len, "%s/%s", basedir, path );
		strncpy( path, buff, len );
		free(buff);
	}
	return path;
}

/**
 * Some ANSI code magic to set the terminal title
 **/
void setTitle(const char* title) {
	char buff[128];
	strncpy(buff, "\033]2;", 128 );
	strncat(buff, title, 128 );
	strncat(buff, "\007\000", 128 );
	fputs(buff, stdout);
	fflush(stdout);
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
	// clear target buffer
	memset( buff, 0, maxlen );

	// Cut off leading spaces and special chars
	while( ( tpos < len ) && ( isspace( text[tpos] ) ) ) tpos++;

	// Filter out all extended characters
	while( ( 0 != text[tpos] )  && ( bpos < ( maxlen-1) ) ) {
//		if( isascii( text[tpos]) ) {
		if( isprint( text[tpos]) ) {
			buff[bpos]=text[tpos];
			bpos++;
		}
		tpos++;
	}

	// Make sure string ends with a 0
	buff[bpos]=0;
	bpos--;

	// Cut off trailing spaces and special chars
	while( ( bpos > 0 ) && ( iscntrl(buff[bpos]) || isspace(buff[bpos] )) ) {
		buff[bpos]=0;
		bpos --;
	}

	return buff;
}

/*
 * Print errormessage, errno and wait for [enter]
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *         F_WARN = print message w/o errno and return
 *         F_FAIL = print message w/o errno and exit
 */
void fail( int error, const char* msg, ... ){
	va_list args;
	va_start( args, msg );
	endwin();
	if(error <= 0 ) {
		fprintf(stderr, "\n");
		vfprintf(stderr, msg, args );
		fprintf( stderr, "\n" );
	}
	else {
		fprintf(stderr, "\n");
		vfprintf(stderr, msg, args );
		fprintf( stderr, "\n ERROR: %i - %s\n", abs(error), strerror( abs(error) ) );
	}
	fprintf(stderr, "Press [ENTER]\n" );
	fflush( stdout );
	fflush( stderr );
	va_end(args);
	while(getc(stdin)!=10);
	if (error != 0 ) exit(error);
	return;
}

/**
 * reads from the fd into the line buffer until either a CR
 * comes or the fd stops sending characters.
 * returns number of read bytes or -1 on overflow.
 */
int readline( char *line, size_t len, int fd ){
	int cnt=0;
	char c;

	while ( 0 != read(fd, &c, 1 ) ) {
		if( cnt < len ) {
			if( '\n' == c ) c=0;
			line[cnt]=c;
			cnt++;
			if( 0 == c ) {
				return cnt;
			}
		} else {
			return -1;
		}
	}

	// avoid returning unterminated strings.
	if( cnt < len ) {
		line[cnt]=0;
		cnt++;
		return cnt;
	} else {
		return -1;
	}

	return cnt;
}

/**
 * checks if text ends with suffix
 * this function is case insensitive
 */
int endsWith( const char *text, const char *suffix ){
	int i, tlen, slen;
	tlen=strlen(text);
	slen=strlen(suffix);
	if( tlen < slen ) {
		return 0;
	}
	for( i=slen; i>0; i-- ) {
		if( tolower(text[tlen-i]) != tolower(suffix[slen-i]) ) {
			return 0;
		}
	}
	return -1;
}

/**
 * checks if text starts with prefix
 * this function is case insensitive
 */
int startsWith( const char *text, const char *prefix ){
	int i, tlen, plen;
	tlen=strlen(text);
	plen=strlen(prefix);
	if( tlen < plen ) {
		return 0;
	}
	for( i=0; i<plen; i++ ) {
		if( tolower(text[i]) != tolower(prefix[i]) ) {
			return 0;
		}
	}

	return -1;
}

/**
 * Check if a file is a music file
 */
int isMusic( const char *name ){
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
int isURL( const char *uri ){
	if( startsWith( uri, "http://" ) || startsWith( uri, "https://" ) ) {
		return -1;
	}
	return 0;
}


/**
 * show activity roller on console
 * this will only show when the global verbosity is larger than 0
 */
void activity( const char *msg ){
	char roller[5]="|/-\\";
	int pos;
	if( _ftverbosity && ( _ftrpos%100 == 0 )) {
		pos=(_ftrpos/100)%4;
		printf( "%s %c\r", msg, roller[pos] ); fflush( stdout );
	}
	_ftrpos=(_ftrpos+1)%400;
}	

/*
 * Inplace conversion of a string to lowercase
 */
static char *toLower( char *text ){
	int i;
	for(i=0;i<strlen(text);i++) text[i]=tolower(text[i]);
	return text;
}

/**
 * works like strncpy but turns every character to lowercase
 */
int strlncpy( char *dest, const char *src, const size_t len ) {
	strncpy( dest, src, len );
	dest=toLower(dest);
	return strlen(dest);
}

/**
 * works like strncat but turns every character to lowercase
 */
int strlncat( char *dest, const char *src, const size_t len ) {
	strncat( dest, src, len );
	dest=toLower(dest);
	return strlen(dest);
}

/**
 * add a line to a file
 */
void addToFile( const char *path, const char *line ) {
	FILE *fp;
	fp=fopen( path, "a" );
	if( NULL == fp ) {
		fail( errno, "Could not open %s for writing ", path );
	}
	fputs( line, fp );
	fputc( '\n', fp );
	fclose( fp );
}


/*
 * internally used to set a bit in a long bitlist
 */
static int setBit( unsigned long pos, strval_t val ){
	int bytepos;
	unsigned char set=0;

	// avoid under/overflow
	if( pos < 0 ) pos=0;
	if( pos > CMP_BITS ) pos=CMP_BITS;

	bytepos=pos/8;
	set = 1<<(pos%8);
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
static int computestrval( const char* str, strval_t strval ){
	unsigned char c1, c2;
	int cnt, max=0;

	// needs at least two characters!
	if( 2 > strlen( str) ) return 0;

	for( cnt=0; cnt < strlen( str )-1; cnt++ ){
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
static unsigned int vecmult( strval_t val1, strval_t val2 ){
	unsigned int result=0;
	int cnt;
	unsigned char c;

	for( cnt=0; cnt<CMP_ARRAYLEN; cnt++ ){
		c=val1[cnt] & val2[cnt];
		while( c != 0 ){
			if( c &  1 ) result++;
			c=c>>1;
		}
	}

	return result;
}

/**
 * Compares two strings and returns the similarity index
 * 100 == most equal
 **/
int fncmp( const char* str1, const char* str2 ){
	strval_t str1val, str2val;
	unsigned int maxval, max1, max2;
	long result;
	float step;

	str1val=calloc( CMP_ARRAYLEN, sizeof( char ) );
	str2val=calloc( CMP_ARRAYLEN, sizeof( char ) );

	max1=computestrval( str1, str1val );
	max2=computestrval( str2, str2val );

	// the max possible matches are defined by the min number of bits set!
	maxval=(max1 < max2) ? max1 : max2;
	if( 0 == maxval ) return -1;

	step=100.0/maxval;

	result=vecmult(  str1val, str2val );

	free( str1val );
	free( str2val );

	return step*result;
}
