/*
 * mpserver.h
 *
 *  Created on: 01.05.2018
 *      Author: foxbow
 */

#ifndef _MPSERVER_H_
#define _MPSERVER_H_

/* basic communication block size */
#define MP_BLKSIZE 512

typedef struct {
	int32_t cmd;
	char *arg;
	int32_t clientid;
} mpReqInfo;

int32_t startServer();

#endif /* _MPSERVER_H_ */
