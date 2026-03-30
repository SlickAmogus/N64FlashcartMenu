/**
 * @file sound.h
 * @brief Menu Sound
 * @ingroup menu
 */

#ifndef SOUND_H__
#define SOUND_H__

#include <stdbool.h>

#define SOUND_SFX_CHANNEL           (0) /**< Channel for sound effects */
#define SOUND_MP3_PLAYER_CHANNEL    (2) /**< Channel for MP3 player sound */


/**
 * @brief Enumeration of available sound effects for menu interactions.
 * 
 * This enumeration defines the different sound effects that can be used
 * for menu interactions.
 */
typedef enum {
    SFX_CURSOR,  /**< Sound effect for cursor movement */
    SFX_ERROR,   /**< Sound effect for error */
    SFX_ENTER,   /**< Sound effect for entering a menu */
    SFX_EXIT,    /**< Sound effect for exiting a menu */
    SFX_SETTING, /**< Sound effect for changing a setting */
} sound_effect_t;

/**
 * @brief Initialize the default sound system.
 * 
 * This function initializes the default sound system, setting up
 * necessary resources and configurations.
 */
void sound_init_default(void);

/**
 * @brief Initialize the MP3 playback system.
 * 
 * This function initializes the MP3 playback system, preparing it
 * for playing MP3 files.
 */
void sound_init_mp3_playback(void);

/**
 * @brief Set the directory to search for custom sound effect files.
 *
 * When set, sound_init_sfx() will look for .wav64 files in this directory
 * before falling back to the built-in ROM sounds.  Call this before
 * sound_init_sfx().
 *
 * @param dir Full path to the effects directory on the SD card, or NULL to
 *            use only the built-in ROM sounds.
 */
void sound_set_sfx_dir(const char *dir);

/**
 * @brief Initialize the sound effects system.
 *
 * This function initializes the sound effects system, setting up
 * necessary resources and configurations for playing sound effects.
 * If a custom effects directory has been set via sound_set_sfx_dir(),
 * files from that directory take precedence over the built-in ROM sounds.
 */
void sound_init_sfx(void);

/**
 * @brief Enable or disable sound effects.
 * 
 * @param enable True to enable sound effects, false to disable.
 */
void sound_use_sfx(bool enable);

/**
 * @brief Play a specified sound effect.
 * 
 * @param sfx The sound effect to play, as defined in sound_effect_t.
 */
void sound_play_effect(sound_effect_t sfx);

/**
 * @brief Deinitialize the sound system.
 * 
 * This function deinitializes the sound system, releasing any resources
 * that were allocated.
 */
void sound_deinit(void);

/**
 * @brief Poll the sound system.
 * 
 * This function polls the sound system, updating its state as necessary.
 */
void sound_poll(void);

#endif /* SOUND_H__ */
