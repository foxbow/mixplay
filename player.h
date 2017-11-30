/*
 * player.h
 *
 *  Created on: 26.04.2017
 *      Author: bweber
 */

#ifndef __PLAYER_H__
#define __PLAYER_H__

#define VOLSTEP 2
#include "config.h"

void setPCommand( mpcmd cmd );
void *reader( void *control );
void *setProfile( void *control );
int  setArgument( const char *arg );

/* must be implemented by the calling app */
void updateUI( mpconfig *control );

#endif /* __PLAYER_H__ */
