/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *	  Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#include "config.h"

/* message request types */
/* return simple player status */
#define MPCOMM_STAT 0
/* return full title/playlist info */
#define MPCOMM_FULLSTAT 1
/* return a search result */
#define MPCOMM_RESULT 2
/* return DNP and favlists */
#define MPCOMM_LISTS 4
/* return immutable configuration */
#define MPCOMM_CONFIG -1

/* default mixplay HTTP port */
#define MP_PORT 2347

char *serializeStatus( unsigned long *count, int clientid, int fullstat );
char *serializeConfig( void );
int setCurClient( int client );
int isCurClient( int client );
void unlockClient( int client );

#endif /* MPCOMM_H_ */
