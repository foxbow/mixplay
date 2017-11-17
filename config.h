/*
 * config.h
 *
 *  Created on: 16.11.2017
 *      Author: bweber
 */

#ifndef CONFIG_H_
#define CONFIG_H_
#include "player.h"

void writeConfig( struct mpcontrol_t *config );
int readConfig( struct mpcontrol_t *config );

#endif /* CONFIG_H_ */
