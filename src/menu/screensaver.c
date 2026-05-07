/**
 * @file screensaver.c
 * @brief Idle screensaver — burn-in protection for CRT/plasma displays
 * @ingroup menu
 */

#include <libdragon.h>
#include <stdlib.h>
#include <string.h>

#include "screensaver.h"
#include "fonts.h"
#include "ui_components/constants.h"

#define BOUNCER_TEXT        "SLICKAMOGUS"
/* Approximate bounding box for the label in pixels.  Slightly generous so the
 * text never clips at the edges; rdpq_text doesn't expose a cheap measure
 * helper here so we estimate from the font's bold glyph width. */
#define BOUNCER_W           (140)
#define BOUNCER_H           (24)
/* Speed in pixels per millisecond.  ~80 px/sec on each axis matches the
 * leisurely pace of the classic DVD logo. */
#define BOUNCER_SPEED_PXMS  (0.08f)

/* Tint cycle — skip white/gray so each bounce is a clearly different colour. */
static const uint8_t bouncer_styles[] = {
    STL_GREEN, STL_BLUE, STL_YELLOW, STL_ORANGE, STL_RED,
};
#define BOUNCER_STYLE_COUNT (sizeof(bouncer_styles) / sizeof(bouncer_styles[0]))

static bool active = false;
static float pos_x, pos_y;
static float vel_x, vel_y;
static int   tint_index = 0;
static uint32_t last_frame_ms = 0;

static float random_velocity_component (void) {
    /* Random sign, fixed magnitude. */
    return (rand() & 1) ? BOUNCER_SPEED_PXMS : -BOUNCER_SPEED_PXMS;
}

void screensaver_init (void) {
    active = false;
    pos_x = 0.0f;
    pos_y = 0.0f;
    vel_x = BOUNCER_SPEED_PXMS;
    vel_y = BOUNCER_SPEED_PXMS;
    tint_index = 0;
    last_frame_ms = 0;
}

void screensaver_deinit (void) {
    active = false;
}

void screensaver_set_active (bool a) {
    if (a && !active) {
        /* Activating — randomise start position and direction. */
        int max_x = DISPLAY_WIDTH  - BOUNCER_W;
        int max_y = DISPLAY_HEIGHT - BOUNCER_H;
        pos_x = (float)(rand() % (max_x > 0 ? max_x : 1));
        pos_y = (float)(rand() % (max_y > 0 ? max_y : 1));
        vel_x = random_velocity_component();
        vel_y = random_velocity_component();
        tint_index = rand() % BOUNCER_STYLE_COUNT;
        last_frame_ms = get_ticks_ms();
    }
    active = a;
}

static void advance_bouncer (uint32_t now_ms) {
    uint32_t dt = (last_frame_ms == 0) ? 16 : (now_ms - last_frame_ms);
    /* Cap dt to avoid huge jumps if the loop stalls (e.g. SD I/O). */
    if (dt > 100) dt = 100;
    last_frame_ms = now_ms;

    pos_x += vel_x * (float)dt;
    pos_y += vel_y * (float)dt;

    bool bounced = false;
    if (pos_x <= 0.0f) {
        pos_x = 0.0f;
        vel_x = -vel_x;
        bounced = true;
    } else if (pos_x + BOUNCER_W >= (float)DISPLAY_WIDTH) {
        pos_x = (float)(DISPLAY_WIDTH - BOUNCER_W);
        vel_x = -vel_x;
        bounced = true;
    }
    if (pos_y <= 0.0f) {
        pos_y = 0.0f;
        vel_y = -vel_y;
        bounced = true;
    } else if (pos_y + BOUNCER_H >= (float)DISPLAY_HEIGHT) {
        pos_y = (float)(DISPLAY_HEIGHT - BOUNCER_H);
        vel_y = -vel_y;
        bounced = true;
    }
    if (bounced) {
        tint_index = (tint_index + 1) % BOUNCER_STYLE_COUNT;
    }
}

void screensaver_draw (surface_t *display) {
    if (!display) {
        return;
    }

    rdpq_attach_clear(display, NULL);

    /* Solid black fill — nothing static visible.  Best case for burn-in. */
    rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
    rdpq_fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    if (active) {
        advance_bouncer(get_ticks_ms());

        rdpq_textparms_t parms = { .style_id = bouncer_styles[tint_index] };
        rdpq_text_print(&parms, FNT_DEFAULT, (int)pos_x, (int)pos_y + BOUNCER_H, BOUNCER_TEXT);
    }

    rdpq_detach_show();
}
