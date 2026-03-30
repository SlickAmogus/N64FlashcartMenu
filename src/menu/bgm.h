/**
 * @file bgm.h
 * @brief Background Music Player
 * @ingroup menu
 */

#ifndef BGM_H__
#define BGM_H__

#include <stdbool.h>

/**
 * @brief Scan the music directory and prepare the shuffled playlist.
 *
 * Looks for .mp3 files in the given directory. Does nothing if the directory
 * does not exist or contains no MP3 files.
 *
 * @param music_dir Full path to the music directory (e.g. "sd:/menu/music").
 */
void bgm_init(const char *music_dir);

/**
 * @brief Stop playback and free all BGM resources.
 */
void bgm_deinit(void);

/**
 * @brief Enable or disable background music.
 *
 * Starts playback immediately when enabling (if files are available), and
 * stops playback immediately when disabling.
 *
 * @param enabled True to enable BGM, false to disable.
 */
void bgm_set_enabled(bool enabled);

/**
 * @brief Temporarily suspend BGM so the music player view can take over.
 *
 * Stops the mp3player and marks BGM as suspended. Call bgm_resume() when
 * the music player view exits.
 */
void bgm_suspend(void);

/**
 * @brief Resume BGM after the music player view has exited.
 */
void bgm_resume(void);

/**
 * @brief Advance track state. Must be called once per main loop iteration.
 *
 * Checks whether the current track has finished and starts the next one.
 * Also drives mp3player_process() while a BGM track is playing.
 */
void bgm_process(void);

#endif /* BGM_H__ */
