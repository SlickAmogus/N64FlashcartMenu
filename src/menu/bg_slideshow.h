/**
 * @file bg_slideshow.h
 * @brief Background image slideshow with optional MPEG1 video support
 * @ingroup menu
 */

#ifndef BG_SLIDESHOW_H__
#define BG_SLIDESHOW_H__

#include <stdbool.h>

/**
 * @brief Scan the backgrounds directory and prepare the slideshow.
 *
 * If a .m1v file is found it is opened as a looping animated background and
 * PNG files are ignored.  Otherwise up to MAX_BG_FILES PNG files are added to
 * a shuffled rotation list and the first image is loaded immediately.
 *
 * Safe to call when the directory does not exist or is empty.
 *
 * @param backgrounds_dir Full path to the backgrounds directory.
 */
void bg_slideshow_init(const char *backgrounds_dir);

/**
 * @brief Stop the slideshow and release all resources.
 */
void bg_slideshow_deinit(void);

/**
 * @brief Set how often the background PNG rotates.
 *
 * @param seconds Rotation interval in seconds.  0 disables rotation.
 *                Typical values: 0, 30, 60, 120, 300.
 */
void bg_slideshow_set_interval(int seconds);

/**
 * @brief Drive the slideshow timer.  Call once per main-loop iteration.
 *
 * Triggers the next PNG decode when the configured interval has elapsed.
 * Does nothing in video mode (the background component handles video frames).
 */
void bg_slideshow_process(void);

#endif /* BG_SLIDESHOW_H__ */
