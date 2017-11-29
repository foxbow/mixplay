/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#include "config.h"

#define MP_MAXCOMLEN (3*NAMELEN+2*10+2*sizeof(int)+sizeof(mpcmd))
#define MP_PORT 2347

struct conninfo_t {
	char *address;
	int   port;
	mpcmd cmd;
};

typedef struct conninfo_t conninfo;

void *netreader( void *control );
void sendCommand( conninfo *info, mpcmd cmd );
size_t serialize( const mpconfig *data, char *buff );
size_t deserialize( mpconfig *data, const char *buff );

#endif /* MPCOMM_H_ */
