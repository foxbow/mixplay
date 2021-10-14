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
int32_t setCurClient(int32_t client);
int32_t isCurClient(int32_t client);
void unlockClient(int32_t client);
int32_t getCurClient();
#endif /* MPCOMM_H_ */
