/*
 * player.h
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
 */

#ifndef __PLAYER_H__
#define __PLAYER_H__
#include <stdint.h>

/* percentage of volume change for mpc_ivol and mpc_dvol */
#define VOLSTEP 2

void *reader();
void *setProfile(void *);
void setStream(const char *const stream, const char *const name);
void sendplay( void );
void stopPlay(void);
void pausePlay(void);
void *killPlayers(int32_t restart);
void setOrder(int32_t);
void setSkipped(void);
int32_t toPlayer(int32_t player, const char *msg);
void cleanTitles(int32_t flags);

#endif /* __PLAYER_H__ */
