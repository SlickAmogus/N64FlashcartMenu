/**
 * @file screensaver.h
 * @brief Idle screensaver — burn-in protection for CRT/plasma displays
 * @ingroup menu
 *
 * After a configurable idle period the menu can switch to a full-screen black
 * background with a single bouncing element, DVD-screensaver style.  Any
 * joypad input deactivates it.  The element bounces off all four edges and,
 * when text is being drawn, cycles colour on each bounce.
 *
 * The bouncer is a PNG image if `/menu/screensaver/bouncer.png` exists on
 * the SD card; otherwise it falls back to a colored text label.  The text
 * itself is user-configurable via the `screensaver_text` key in
 * `config.ini`'s `[menu]` section.
 */

#ifndef SCREENSAVER_H__
#define SCREENSAVER_H__

#include <libdragon.h>
#include <stdbool.h>

/**
 * @brief Initialize the screensaver.
 *
 * Sets the fallback text label and (asynchronously) attempts to load
 * @p image_path as a PNG.  Either argument may be NULL; if @p text is NULL
 * a default label is used, and if @p image_path is NULL no image load is
 * attempted.  Image dimensions are capped to keep the working buffer small.
 */
void screensaver_init (const char *image_path, const char *text);

void screensaver_deinit (void);

/**
 * @brief Replace the fallback bouncer text.  Copies internally; the caller
 *        retains ownership of @p text.  NULL restores the default.
 */
void screensaver_set_text (const char *text);

/**
 * @brief Drive the asynchronous PNG load forward.  Cheap when nothing's
 *        pending — call once per frame from the main loop.
 */
void screensaver_process (void);

/**
 * @brief Toggle the active state.  On activation the bouncer's position and
 *        velocity are randomised; on deactivation this is a no-op.
 */
void screensaver_set_active (bool active);

/**
 * @brief Draw one full-screen frame: black fill plus the bouncing element.
 *        Self-contained — performs its own rdpq_attach / rdpq_detach_show.
 *        Time advances based on the wall-clock delta since the last call.
 */
void screensaver_draw (surface_t *display);

/** @brief Background styles available for the screensaver. */
typedef enum {
    SCREENSAVER_BG_BLACK    = 0,
    SCREENSAVER_BG_NAVY     = 1,
    SCREENSAVER_BG_CYAN     = 2,
    SCREENSAVER_BG_PURPLE   = 3,
    SCREENSAVER_BG_RED      = 4,
    SCREENSAVER_BG_GREEN    = 5,
    SCREENSAVER_BG_STARFIELD = 6,
    SCREENSAVER_BG_COUNT    = 7,
} screensaver_bg_t;

/**
 * @brief Set the screensaver background type.  Takes effect on the next draw.
 *        Values outside the valid range are clamped to BLACK.
 */
void screensaver_set_bg (int bg);

#endif /* SCREENSAVER_H__ */
