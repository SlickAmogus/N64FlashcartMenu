/**
 * @file screensaver.h
 * @brief Idle screensaver — burn-in protection for CRT/plasma displays
 * @ingroup menu
 *
 * After a configurable idle period the menu can switch to a full-screen black
 * background with a single bouncing colored label, DVD-screensaver style.
 * Any joypad input deactivates it.  The bouncer's color cycles each time it
 * bounces off an edge.
 */

#ifndef SCREENSAVER_H__
#define SCREENSAVER_H__

#include <libdragon.h>
#include <stdbool.h>

void screensaver_init (void);
void screensaver_deinit (void);

/** Toggle the active state.  On activation the bouncer's position/velocity
 *  are randomised; on deactivation this is a no-op. */
void screensaver_set_active (bool active);

/** Draw a full-screen frame: black fill + bouncing label.  Self-contained:
 *  performs its own rdpq_attach / rdpq_detach_show.  Time advances based on
 *  the wall-clock delta since the last call. */
void screensaver_draw (surface_t *display);

#endif /* SCREENSAVER_H__ */
