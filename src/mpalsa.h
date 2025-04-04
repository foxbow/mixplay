/*
 * mpalsa.h
 *
 *  Created on: 05.08.2021
 *	  Author: bweber
 */

#ifndef _MPAUDIO_H_
#define _MPAUDIO_H_
#include <stdint.h>
#include <stdbool.h>

/* special volume values, if these change, the changes need to be reflected 
 * in mixplayd.js too */
#define MUTED    (-1)
#define AUTOMUTE (-2)
#define NOAUDIO  (-3)
#define LINEOUT  (-4)			// either mute or 100%

#define DEFAULT_VOLUME (80)

long controlVolume(long volume, bool absolute);
long toggleMute(void);
void closeAudio(void);

/*
 * naming wrappers for controlVolume
 */
#define setVolume(v)	controlVolume(v,true)
#define getVolume()	    controlVolume(0,false)
#define adjustVolume(d) controlVolume(d,false)
#endif
