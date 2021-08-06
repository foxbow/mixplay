/*
 * mpalsa.h
 *
 *  Created on: 05.08.2021
 *	  Author: bweber
 */

#ifndef _MPAUDIO_H_
#define _MPAUDIO_H_

long controlVolume(long volume, int absolute);
long toggleMute();
void closeAudio();

/*
 * naming wrappers for controlVolume
 */
#define setVolume(v)	controlVolume(v,1)
#define getVolume()	 controlVolume(0,0)
#define adjustVolume(d) controlVolume(d,0)

#endif
