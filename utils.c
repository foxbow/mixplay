#include "utils.h"
#include "musicmgr.h"
#include "ncutils.h"

// Represents a string as a bit array
typedef unsigned char* strval_t;

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
**/
char *strip( char *buff, const char *text, const size_t maxlen ) {
	int len=strlen( text );
	int pos=0;

	// clear target buffer
	memset( buff, 0, maxlen );

	// Cut off leading spaces and special chars
	while( ( pos < len ) && ( isspace( text[pos] ) ) ) pos++;

	// Copy the remains into buff
	strncpy( buff, text+pos, maxlen );
	buff[maxlen]=0;

	pos = strlen(buff);

	// Cut off trailing spaces and special chars
	while( ( pos > 0 ) && ( iscntrl(buff[pos]) || isspace(buff[pos] )) ) {
		buff[pos]=0;
		pos --;
	}

	return buff;
}


/*
 * Print errormessage, errno and quit
 * msg - Message to print
 * info - second part of the massage, for instance a variable
 * error - errno that was set
 *          0 = print message w/o error and return
 *         -1 = print message w/o error and exit
 */
void fail( const char* msg, const char* info, int error ){
	endwin();
	if(error <= 0 )
		fprintf(stderr, "\n%s %s\n", msg, info );
	else
		fprintf(stderr, "\n%s %s\nERROR: %i - %s\n", msg, info, abs(error), strerror( abs(error) ) );
	fprintf(stderr, "Press [ENTER]\n" );
	fflush( stdout );
	fflush( stderr );
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
char *toLower( char *text ){
	int i;
	for(i=0;i<strlen(text);i++) text[i]=tolower(text[i]);
	return text;
}


/**
 * add a line to a playlist/blacklist
 */
void addToList( const char *path, const char *line ) {
	FILE *fp;
	fp=fopen( path, "a" );
	if( NULL == fp ) {
		fail( "Could not open list for writing ", path, errno );
	}
	fputs( line, fp );
	fputc( '\n', fp );
	fclose( fp );
}


/*
 * sets a bit in a long bitlist
 */
static void setBit( int pos, strval_t val ){
	int bytepos;
	unsigned char set=0;

	// avoid under/overflow
	if( pos < 0 ) pos=0;
	if( pos > CMP_ARRAYLEN ) pos=CMP_ARRAYLEN;

	bytepos=pos/8;
	set = 1<<(pos%8);
	val[bytepos]|=set;
}

static int computestrval( const char* str, strval_t strval ){
	char c1, c2;
	int cnt, max=0;

	if( 3 > strlen( str) ) return 0;

	for( cnt=0; cnt < strlen( str )-1; cnt++ ){
		c1=str[cnt];
		c2=str[cnt+1];
		if( isalpha( c1 )  && isalpha( c2 ) ){
			c1=tolower( c1 );
			c2=tolower( c2 );
			c1=c1-'a';
			c2=c2-'a';
			setBit( c1*26+c2, strval );
			max++;
		}
	}
	return max;
}

static int vecmult( strval_t val1, strval_t val2 ){
	int result=0;
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
	int maxval, max1, max2;
	long result;
	float step;

	str1val=calloc( CMP_ARRAYLEN, sizeof( char ) );
	str2val=calloc( CMP_ARRAYLEN, sizeof( char ) );

	max1=computestrval( str1, str1val );
	max2=computestrval( str2, str2val );

	// the max possible matches are defined by the min number of bits set!
	maxval=(max1 < max2) ? max1 : max2;

	if( maxval < 4 ) return -1;
	step=100.0/maxval;

	result=vecmult(  str1val, str2val );

	free( str1val );
	free( str2val );

	return step*result;
}
