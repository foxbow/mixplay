/*
 * player.h
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
 */

#ifndef __PLAYER_H__
#define __PLAYER_H__
#include "config.h"

/* percentage of volume change for mpc_ivol and mpc_dvol */
#define VOLSTEP 2

void setCommand(mpcmd_t cmd, char *arg);
void *reader(void *);
void *setProfile(void *);
void setStream(const char *const stream, const char *const name);

#endif /* __PLAYER_H__ */
