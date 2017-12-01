/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#define MP_COMVER 3
#include "config.h"

/*
 * max size of a title on the net
 * - artist, album, title [NAMELEN]
 * - flags                [int]
 */
#define MP_ENTRYLEN ((3*NAMELEN)+sizeof(int))

/*
 * max size of an update packet
 * - version					1*[int]
 * 3 titles  					3*[MP_ENTRYLEN]
 * - intime, remtime 			2*[10]
 * - percent, volume, status,
 * + playstream, messagelen		5*[int]
 * - message					128
 * - 0 Byte						1
 */
#define MP_MAXCOMLEN (sizeof(int)+3*MP_ENTRYLEN+2*10+5*sizeof(int)+128+1)
#define MP_PORT 2347

void *netreader( void *control );
void setSCommand( mpcmd cmd );
size_t serialize( const mpconfig *data, char *buff );
size_t deserialize( mpconfig *data, const char *buff );

#endif /* MPCOMM_H_ */
