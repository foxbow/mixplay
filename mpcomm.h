/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#define MP_COMVER 6
#include "config.h"

#define MP_MAXCOMLEN 4096
#define MP_PORT 2347

void *netreader( void *control );
void setSCommand( mpcmd cmd );
size_t serialize( const mpconfig *data, char *buff, long *count, int clientid );
size_t deserialize( mpconfig *data, char *json );
void setCurClient( int client );
void unlockClient( int client );

#endif /* MPCOMM_H_ */
