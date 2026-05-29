/**
 * @file bgm.h
 * @brief Background Music Player
 * @ingroup menu
 */

#ifndef BGM_H__
#define BGM_H__

#include <stdbool.h>

/** Maximum number of MP3 files loaded from the music directory. */
#define BGM_MAX_FILES   50

/**
 * @brief Scan the music directory and prepare the shuffled playlist.
 *
 * Looks for .mp3 files in the given directory. Does nothing if the directory
 * does not exist or contains no MP3 files.
 *
 * @param music_dir Full path to the music directory (e.g. "sd:/menu/music").
 */
void bgm_init(const char *music_dir);

/** @brief Return the number of MP3 files found by bgm_init. */
int bgm_get_file_count(void);

/**
 * @brief Return the basename (filename only, including extension) of the
 *        file at @p index.  Returns NULL if index is out of range.  The
 *        returned pointer is valid until bgm_deinit.
 */
const char *bgm_get_basename(int index);

/**
 * @brief Pin playback to a specific track.  Pass -1 to return to shuffle.
 *        If BGM is currently playing the old track is stopped immediately;
 *        bgm_process will start the new selection on the next call.
 */
void bgm_set_track(int index);

/** @brief Return the currently pinned track index, or -1 if in shuffle mode. */
int bgm_get_current_track(void);

/**
 * @brief Pin playback by filename (basename with extension, e.g. "song.mp3").
 *        Searches the loaded file list case-insensitively.  Falls back to
 *        shuffle if the name is NULL, empty, or not found.
 */
void bgm_set_track_by_name(const char *name);

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
