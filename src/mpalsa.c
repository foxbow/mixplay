/*
 * mpalsa.c
 *
 * contains the ALSA wrapper to control volume and mute settings.
 *
 *  Created on: 05.08.2021
 *	  Author: bweber
 */

#include <alsa/asoundlib.h>
#include "mpalsa.h"
#include "config.h"

static snd_mixer_t *_handle = NULL;
static snd_mixer_elem_t *_elem = NULL;
static snd_mixer_selem_id_t *_sid = NULL;

/**
 * disconnects from the mixer and frees all resources
 */
void closeAudio() {
	if (_handle != NULL) {
		snd_mixer_detach(_handle, "default");
		snd_mixer_close(_handle);
		snd_config_update_free_global();
		/* apparently _sid has already been free()d here */
		// snd_mixer_selem_id_free(_sid);
		_handle = NULL;
		_elem = NULL;
	}
}

/**
 * tries to connect to the mixer
 */
static long openAudio(char const *const channel) {
	if (channel == NULL || strlen(channel) == 0) {
		/* shouldn't this be deadly? */
		addMessage(0, "No audio channel set");
		return NOAUDIO;
	}

	int rv = snd_mixer_open(&_handle, 0);

	if (rv < 0) {
		/* shouldn't this be deadly? */
		addMessage(0, "No ALSA support");
		addError(-rv);
		return NOAUDIO;
	}

	usleep(250000);				// TODO: removeme!!

	if (_handle == NULL) {
		// can this even happen?
		addMessage(0, "No more ALSA support?");
		return NOAUDIO;
	}

	if (snd_mixer_attach(_handle, "default") < 0) {
		/* shouldn't this be deadly? */
		addMessage(0, "Can't attach to default mixer!");
		return NOAUDIO;
	}
	if (snd_mixer_selem_register(_handle, NULL, NULL) < 0) {
		/* shouldn't this be deadly? */
		addMessage(0, "Can't register handle");
		return NOAUDIO;
	}
	snd_mixer_load(_handle);

	snd_mixer_selem_id_alloca(&_sid);
	snd_mixer_selem_id_set_index(_sid, 0);
	snd_mixer_selem_id_set_name(_sid, channel);
	_elem = snd_mixer_find_selem(_handle, _sid);
	/**
	 * for some reason this can't be free'd explicitly.. ALSA is weird!
	 */
	if (_elem == NULL) {
		addMessage(0, "Can't find channel %s!", channel);
		closeAudio();
		return NOAUDIO;
	}

	return 0;
}

/**
 * adjusts the master volume
 * if volume is 0 the current volume is returned without changing it
 * otherwise it's changed by 'volume'
 * if abs is 0 'volume' is regarded as a relative value
 * if ALSA does not work or the current card cannot be selected -1 is returned
 */
long controlVolume(long volume, bool absolute) {
	long min, max;
	int32_t mswitch = 0;
	long retval = 0;
	char *channel;
	mpconfig_t *config;

	config = getConfig();
	channel = config->channel;

	if (config->volume == NOAUDIO) {
		addMessage(0, "Volume control is not supported!");
		return NOAUDIO;
	}

	if (_handle == NULL) {
		if (openAudio(channel) == NOAUDIO) {
			config->volume = NOAUDIO;
			return NOAUDIO;
		}
	}

	/* for some reason this can happen and lead to an assert error
	 * Give a slight warning and return default as this commonly only
	 * happens when setting the initial volume of the new profile */
	if (_elem == NULL) {
		addMessage(1, "Volume control is not fully initialized!");
		if (absolute)
			return volume;
		return config->volume;
	}

	/* if audio is muted, don't change a thing */
	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem, SND_MIXER_SCHN_FRONT_LEFT,
											&mswitch);
		if (mswitch == 0) {
			/* if muted from the outside, set to MUTED, otherwise don't 
			 * accidentially overwrite an AUTOMUTE */
			if (config->volume > 0)
				config->volume = MUTED;
			return MUTED;
		}
	}

	snd_mixer_selem_get_playback_volume_range(_elem, &min, &max);
	if (absolute != 0) {
		retval = volume;
	}
	else {
		snd_mixer_handle_events(_handle);
		snd_mixer_selem_get_playback_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT,
											&retval);
		retval = ((retval * 100) / max) + 1;
		retval += volume;
	}

	if (config->lineout) {
		retval = LINEOUT;
		snd_mixer_selem_set_playback_volume_all(_elem, max);
	}
	else {
		if (retval < 0)
			retval = 0;
		if (retval > 100)
			retval = 100;
		snd_mixer_selem_set_playback_volume_all(_elem, (retval * max) / 100);
	}
	config->volume = retval;
	return retval;
}

/*
 * toggles the mute state
 * returns NOMUTE if mute is not supported
 *         MUTED  if mute was enabled
 *         the current volume on unmute
 */
long toggleMute() {
	mpconfig_t *config = getConfig();
	int32_t mswitch;

	if (config->volume == NOAUDIO) {
		return NOAUDIO;
	}
	if (_handle == NULL) {
		if (openAudio(config->channel) != 0) {
			config->volume = NOAUDIO;
			return NOAUDIO;
		}
	}
	if (_elem == NULL) {
		/* make this one a pop-up, so the user has a clue why nothing 
		 * happened when he clicked the mute button */
		addMessage(-1, "Volume control is not fully initialized!");
		return config->volume;
	}

	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem,
											SND_MIXER_SCHN_FRONT_LEFT,
											&mswitch);
		if (mswitch == 1) {
			snd_mixer_selem_set_playback_switch_all(_elem, 0);
			config->volume = MUTED;
		}
		else {
			snd_mixer_selem_set_playback_switch_all(_elem, 1);
			config->volume = getVolume();
		}
	}
	else {
		return NOAUDIO;
	}

	return config->volume;
}
