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

char *serializeStatus( int clientid, int fullstat );
int setCurClient( int client );
int isCurClient( int client );
void unlockClient( int client );
int getCurClient();
#endif /* MPCOMM_H_ */
