/*
 * mpgutils.h
 *
 *  Created on: 04.10.2016
 *      Author: bweber
 */

#ifndef MPGUTILS_H_
#define MPGUTILS_H_

#include "musicmgr.h"

int fillTagInfo( const char *basedir, struct entry_t *title );
/* int tagRun( const char *basedir, struct entry_t *base ); */

#endif /* MPGUTILS_H_ */
