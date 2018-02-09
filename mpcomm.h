/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#define MPCOMM_VER 10
#include "config.h"

#define MPCOMM_STAT 1
#define MPCOMM_CONFIG 2
#define MPCOMM_FULLSTAT 3

#define MP_MAXCOMLEN 4096
#define MP_PORT 2347

void *netreader( void *control );
void setSCommand( mpcmd cmd );
size_t serializeStatus( char *buff, long *count, int clientid, int fullstat );
size_t serializeConfig( char *buff );
void setCurClient( int client );
void unlockClient( int client );

#endif /* MPCOMM_H_ */
