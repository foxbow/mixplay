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

typedef enum {
	filenotfound = -2,
	illegal = -1,
	none = 0,
	get = 1,
	post = 2,
	getfile = 3,
	postfile = 4
} mpMethod;

typedef struct {
	int32_t cmd;
	char *arg;
	int32_t clientid;
	mpMethod method;
	char *data;					// filename for GET or boundary for multipart
} mpReqInfo;

int32_t startServer();

#endif /* _MPSERVER_H_ */
