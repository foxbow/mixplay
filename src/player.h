/*
 * player.h
 *
 *  Created on: 26.04.2017
 *	  Author: bweber
 */

#ifndef __PLAYER_H__
#define __PLAYER_H__
#include "config.h"

#define VOLSTEP 2

void setCommand( mpcmd_t cmd );
void *reader( );
void *setProfile( );
void setStream( const char* stream, const char *name );

#endif /* __PLAYER_H__ */
