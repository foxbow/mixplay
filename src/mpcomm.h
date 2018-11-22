/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *	  Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#include "config.h"

#define MPCOMM_STAT 1
#define MPCOMM_CONFIG 2
#define MPCOMM_FULLSTAT 3
#define MPCOMM_RESULT 4
#define MPCOMM_SKIP 5

#define MP_BLKSIZE 512
#define MP_PORT 2347

void setSCommand( mpcmd cmd );
char *serializeStatus( unsigned long *count, int clientid, int fullstat );
char *serializeConfig( void );
int setCurClient( int client );
int isCurClient( int client );
void unlockClient( int client );

#endif /* MPCOMM_H_ */
