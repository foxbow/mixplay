/*
 * mpserver.h
 *
 *  Created on: 01.05.2018
 *      Author: foxbow
 */

#ifndef _MPSERVER_H_
#define _MPSERVER_H_

#include "musicmgr.h"			// for mptitle_t

/* basic communication block size */
#define MP_BLKSIZE 1024

int32_t startServer();

#endif /* _MPSERVER_H_ */
