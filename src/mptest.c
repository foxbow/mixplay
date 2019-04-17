#include <stdio.h>
#include <stdlib.h>
#include "epasupp.h"
#include "utils.h"
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

void addMessage( int v, const char* msg, ... ) {
	va_list args;
	va_start( args, msg );
	printf("mptest%i: ",v);
	vfprintf( stdout, msg, args );
	fprintf( stdout, "\n" );
	va_end( args );
}

void fail( const int error, const char* msg, ... ) {
	va_list args;
	va_start( args, msg );

	fprintf( stdout, "\n" );
	printf("mptest: ");
	vfprintf( stdout, msg, args );
	fprintf( stdout, "\n" );
	if( error > 0 ) {
		fprintf( stdout, "ERROR: %i - %s\n", abs( error ), strerror( abs( error ) ) );
	}
	va_end( args );
	epsPoweroff();
	exit( error );
}

void epsDrawPat( epsmap map, unsigned xp, unsigned yp, const char pat[16][16] ) {
	unsigned x, y;
	unsigned char *numpat;
	numpat=falloc(32,1);
	for( x=0; x<16; x++ ){
		for( y=0; y<16; y++ ) {
			if( pat[15-y][x] != '.' ) {
				epsSetPixel( map, xp+x, yp+y );
				numpat[(x/8)+(2*y)]|=1<<(x%8);
			}
		}
	}
	printf("numpat={ ");
	for( x=0; x<32; x++ ) {
		printf("0x%02x, ", numpat[x] );
	}
	printf("}\n");
	free(numpat);
}

int main( int argc, char **argv ) {
	int i;
	epsymbol s;
	const char pat[16][16]={
		"................",
		".....xxxxxx.....",
		"...xxxxxxxxxx...",
		"...xx..xx..xx...",
		"...xxx.xx.xxx...",
		"...xxxxxxxxxx...",
		"....xxx..xxx....",
		".....xxxxxx.....",
		".....xx.x.x.....",
		"......xxxx......",
		"..xxx......xxx..",
		"....xxx..xxx....",
		"......xxxx......",
		"....xxx..xxx....",
		"..xxx......xxx..",
		"................" };

	printf("Testing display functions..\n");
	epsSetup();
/*
	printf("Boxen!\n");
	epsBox(  epm_black, 100, 40, 120, 60, 0 );
	epsBox(  epm_black, 40, 100, 60, 120, 1 );

	epsBox(  epm_red, 0, 30, 10, 50, 0 );
	epsBox(  epm_red, 0, 90, 10, 110, 1 );

	epsBox(  epm_black, 0, 0, EPHEIGHT-2, EPWIDTH-2, 0 );
	epsBox(  epm_red, 2, 2, EPHEIGHT-4, EPWIDTH-4, 0 );
*/
	printf("Pattern!\n");
	epsDrawPat(  epm_black, 30, 30, pat );

	printf("Symbols!\n");
	for( s=0; s<ep_max; s++ ) {
		epsDrawSymbol(  epm_black, 16*(s+1), (Y_MAX-20), s );
	}

	printf("Lines..\n");

	for( i=0; i<40; i+=2 ) {
		epsLine(  epm_black, 108+i, 20, 183+i, 88 );
		epsLine(  epm_red, 183+i, 88, 108+i, 156 );
	}
	/*
	epsLine(  epm_black, 183, 88, 223, 88 );

	epsLine(  epm_black, 10, 10, 50, 50 );
	epsLine(  epm_red, 50, 10, 55, 20 );
	epsLine(  epm_red, 75, 10, 125, 15 );
*/

	printf( "Text\n" );
	for( s=0; s<20; s++ ) {
		epsDrawChar(  epm_black, 9*(s+1), (Y_MAX-40), s+32, 0 );
	}
/*
	epsDrawString(  epm_black, 80, 80, "foxbow@web.de", 0 );
	epsDrawString(  epm_red, 80, 60, "foxbow", 1 );
	epsDrawString(  epm_red, 80, 20, "fox", 2 );
*/
	printf("Showtime!\n");

	epsDisplay( );

	printf("partial..\n");
	epsWipe( epm_black, 0, 0, 16, 16 );
	epsDrawSymbol(  epm_black, 0, 0, ep_fav );
	epsPartialDisplay( 0, 0, 16, 16 );

	printf("partial2\n");
	epsWipe(  epm_black, 20, 10, 16, 16 );
	epsDrawSymbol(  epm_black, 5, 80, ep_dnp );
	epsPartialDisplay( 5, 80, 16, 16 );

/*
	epsBox( epm_red, 4,40,20,76,0);
	epsDisplay( );
*/

	printf("Done..\n");
	epsPoweroff();
	return 0;
}
