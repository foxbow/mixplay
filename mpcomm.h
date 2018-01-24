/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#define MP_COMVER 8
#include "config.h"

#define MP_MAXCOMLEN 4096
#define MP_PORT 2347

void *netreader( void *control );
void setSCommand( mpcmd cmd );
size_t serializeStatus( char *buff, long *count, int clientid );
size_t serializeConfig( char *buff );
void setCurClient( int client );
void unlockClient( int client );

#endif /* MPCOMM_H_ */
