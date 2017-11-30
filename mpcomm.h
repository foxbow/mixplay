/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *      Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#include "config.h"

/*
 * max size of a title on the net
 * - artist, album, title [NAMELEN]
 * - flags                [int]
 */
#define MP_ENTRYLEN ((3*NAMELEN)+sizeof(int))

/*
 * max size of an update packet
 * 3 titles  								3*[MP_ENTRYLEN]
 * - intime, remtime 						2*[10]
 * - percent, volume, status, playstream	4*[int]
 * - 0 Byte									1
 */
#define MP_MAXCOMLEN (3*MP_ENTRYLEN+2*10+4*sizeof(int)+1)
#define MP_PORT 2347

void *netreader( void *control );
void setSCommand( mpcmd cmd );
size_t serialize( const mpconfig *data, char *buff );
size_t deserialize( mpconfig *data, const char *buff );

#endif /* MPCOMM_H_ */
