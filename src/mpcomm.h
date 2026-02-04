/*
 * mpcomm.h
 *
 *  Created on: 29.11.2017
 *	  Author: bweber
 */

#ifndef MPCOMM_H_
#define MPCOMM_H_

#include "config.h"
#include "json.h"

char *serializeStatus(int32_t clientid, int32_t fullstat);
void lockClient(int32_t client);
bool isCurClient(int32_t client);
void unlockClient(int32_t client);
void debugClient();
int32_t getCurClient();
#endif /* MPCOMM_H_ */
