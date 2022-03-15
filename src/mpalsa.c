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

/**
 * disconnects from the mixer and frees all resources
 */
void closeAudio() {
	if (_handle != NULL) {
		snd_mixer_detach(_handle, "default");
		snd_mixer_close(_handle);
		snd_config_update_free_global();
		_handle = NULL;
		_elem = NULL;
	}
}

/**
 * tries to connect to the mixer
 */
static long openAudio(char const *const channel) {
	snd_mixer_selem_id_t *sid = NULL;

	if (channel == NULL || strlen(channel) == 0) {
		addMessage(0, "No audio channel set");
		return -1;
	}

	snd_mixer_open(&_handle, 0);
	if (_handle == NULL) {
		addMessage(1, "No ALSA support");
		return -1;
	}

	snd_mixer_attach(_handle, "default");
	snd_mixer_selem_register(_handle, NULL, NULL);
	snd_mixer_load(_handle);

	snd_mixer_selem_id_alloca(&sid);
	snd_mixer_selem_id_set_index(sid, 0);
	snd_mixer_selem_id_set_name(sid, channel);
	_elem = snd_mixer_find_selem(_handle, sid);
	/**
	 * for some reason this can't be free'd explicitly.. ALSA is weird!
	 * snd_mixer_selem_id_free(_sid);
	 */
	if (_elem == NULL) {
		addMessage(0, "Can't find channel %s!", channel);
		closeAudio();
		return -1;
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
long controlVolume(long volume, int32_t absolute) {
	long min, max;
	int32_t mswitch = 0;
	long retval = 0;
	char *channel;
	mpconfig_t *config;

	config = getConfig();
	channel = config->channel;

	if (config->volume == -1) {
		addMessage(0, "Volume control is not supported!");
		return -1;
	}

	if (_handle == NULL) {
		if (openAudio(channel) != 0) {
			config->volume = -1;
			return -1;
		}
	}

	/* for some reason this can happen and lead to an assert error
	 * Give a slight warning and return default  */
	if (_elem == NULL) {
		addMessage(1, "Volume control is not fully initialized!");
		return config->volume;
	}

	/* if audio is muted, don't change a thing */
	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem, SND_MIXER_SCHN_FRONT_LEFT,
											&mswitch);
		if (mswitch == 0) {
			config->volume = -2;
			return -2;
		}
	}

	snd_mixer_selem_get_playback_volume_range(_elem, &min, &max);
	if (absolute != 0) {
		if (retval < 0) {
			return config->volume;
		}
		retval = volume;
	}
	else {
		snd_mixer_handle_events(_handle);
		snd_mixer_selem_get_playback_volume(_elem, SND_MIXER_SCHN_FRONT_LEFT,
											&retval);
		retval = ((retval * 100) / max) + 1;
		retval += volume;
	}

	if (retval < 0)
		retval = 0;
	if (retval > 100)
		retval = 100;
	snd_mixer_selem_set_playback_volume_all(_elem, (retval * max) / 100);
	config->volume = retval;
	return retval;
}

/*
 * toggles the mute states
 * returns -1 if mute is not supported
 *         -2 if mute was enabled
 *         the current volume on unmute
 */
long toggleMute() {
	mpconfig_t *config = getConfig();
	int32_t mswitch;

	if (config->volume == -1) {
		return -1;
	}
	if (_handle == NULL) {
		if (openAudio(config->channel) != 0) {
			config->volume = -1;
			return -1;
		}
	}
	if (_elem == NULL) {
		addMessage(-1, "Volume control is not fully initialized!");
		return config->volume;
	}

	if (snd_mixer_selem_has_playback_switch(_elem)) {
		snd_mixer_selem_get_playback_switch(_elem, SND_MIXER_SCHN_FRONT_LEFT,
											&mswitch);
		if (mswitch == 1) {
			snd_mixer_selem_set_playback_switch_all(_elem, 0);
			config->volume = -2;
		}
		else {
			snd_mixer_selem_set_playback_switch_all(_elem, 1);
			config->volume = getVolume();
		}
	}
	else {
		return -1;
	}

	return config->volume;
}
