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
#include "png_decoder.h"
#include "ui_components/constants.h"

#define DEFAULT_TEXT            "SLICKAMOGUS"
/* Per-glyph horizontal advance (Firple-Bold at default size).  Used as a
 * cheap proxy for the text label's width; libdragon doesn't expose a quick
 * measure helper at this layer. */
#define BOUNCER_TEXT_CHAR_W     (12)
#define BOUNCER_TEXT_H          (24)

/* PNG dimension cap.  A 320x240 RGBA decode buffer is ~300 KB which fits
 * even on a base 4 MB N64; larger images simply fail to decode and the
 * screensaver falls back to text. */
#define MAX_BOUNCER_PNG_W       (320)
#define MAX_BOUNCER_PNG_H       (240)

#define BOUNCER_SPEED_PXMS      (0.08f)

static const uint8_t bouncer_styles[] = {
    STL_GREEN, STL_BLUE, STL_YELLOW, STL_ORANGE, STL_RED,
};
#define BOUNCER_STYLE_COUNT (sizeof(bouncer_styles) / sizeof(bouncer_styles[0]))

static bool active = false;
static char *bouncer_text = NULL;
static surface_t *bouncer_image = NULL;
static char *pending_image_path = NULL;
static bool image_decoding = false;
static bool image_pending = false;

static int bouncer_w = 0;
static int bouncer_h = 0;

static float pos_x, pos_y;
static float vel_x, vel_y;
static int   tint_index = 0;
static uint32_t last_frame_ms = 0;


static float random_velocity_component (void) {
    return (rand() & 1) ? BOUNCER_SPEED_PXMS : -BOUNCER_SPEED_PXMS;
}

static void recompute_dimensions (void) {
    if (bouncer_image && bouncer_image->width > 0 && bouncer_image->height > 0) {
        bouncer_w = bouncer_image->width;
        bouncer_h = bouncer_image->height;
    } else {
        int len = bouncer_text ? (int)strlen(bouncer_text) : (int)strlen(DEFAULT_TEXT);
        if (len < 1) len = 1;
        bouncer_w = len * BOUNCER_TEXT_CHAR_W + 8;
        bouncer_h = BOUNCER_TEXT_H;
    }
    /* Clamp position so a smaller bouncer doesn't get stuck off-screen. */
    if (pos_x < 0) pos_x = 0;
    if (pos_y < 0) pos_y = 0;
    if (pos_x + bouncer_w > DISPLAY_WIDTH)  pos_x = DISPLAY_WIDTH  - bouncer_w;
    if (pos_y + bouncer_h > DISPLAY_HEIGHT) pos_y = DISPLAY_HEIGHT - bouncer_h;
}

static void png_load_callback (png_err_t err, surface_t *image, void *data) {
    (void)data;
    image_decoding = false;
    if (err == PNG_OK && image) {
        if (bouncer_image) {
            surface_free(bouncer_image);
            free(bouncer_image);
        }
        bouncer_image = image;
        recompute_dimensions();
    } else if (image) {
        surface_free(image);
        free(image);
    }
    /* On any failure we silently keep the text fallback — no log spam. */
}

static void try_start_image_load (void) {
    if (!pending_image_path || image_decoding) {
        return;
    }
    png_err_t err = png_decoder_start(pending_image_path,
                                       MAX_BOUNCER_PNG_W, MAX_BOUNCER_PNG_H,
                                       png_load_callback, NULL);
    if (err == PNG_OK) {
        image_decoding = true;
        image_pending = false;
    } else if (err == PNG_ERR_BUSY) {
        image_pending = true;  /* retry later */
    } else {
        /* Permanent failure (no file, bad file, OOM): stop trying. */
        image_pending = false;
        free(pending_image_path);
        pending_image_path = NULL;
    }
}


void screensaver_init (const char *image_path, const char *text) {
    active = false;
    pos_x = 0.0f;
    pos_y = 0.0f;
    vel_x = BOUNCER_SPEED_PXMS;
    vel_y = BOUNCER_SPEED_PXMS;
    tint_index = 0;
    last_frame_ms = 0;

    screensaver_set_text(text);

    if (image_path && image_path[0]) {
        if (pending_image_path) free(pending_image_path);
        pending_image_path = strdup(image_path);
        /* png_decoder_start returns PNG_ERR_NO_FILE if the file is missing,
         * which we treat as a permanent failure and silently fall back to text. */
        try_start_image_load();
    }
}

void screensaver_deinit (void) {
    active = false;
    if (bouncer_text) {
        free(bouncer_text);
        bouncer_text = NULL;
    }
    if (bouncer_image) {
        surface_free(bouncer_image);
        free(bouncer_image);
        bouncer_image = NULL;
    }
    if (pending_image_path) {
        free(pending_image_path);
        pending_image_path = NULL;
    }
    image_decoding = false;
    image_pending = false;
}

void screensaver_set_text (const char *text) {
    if (bouncer_text) {
        free(bouncer_text);
    }
    bouncer_text = strdup((text && text[0]) ? text : DEFAULT_TEXT);
    recompute_dimensions();
}

void screensaver_process (void) {
    if (image_pending && !image_decoding) {
        try_start_image_load();
    }
}

void screensaver_set_active (bool a) {
    if (a && !active) {
        recompute_dimensions();
        int max_x = DISPLAY_WIDTH  - bouncer_w;
        int max_y = DISPLAY_HEIGHT - bouncer_h;
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
    if (dt > 100) dt = 100;
    last_frame_ms = now_ms;

    pos_x += vel_x * (float)dt;
    pos_y += vel_y * (float)dt;

    bool bounced = false;
    if (pos_x <= 0.0f) {
        pos_x = 0.0f;
        vel_x = -vel_x;
        bounced = true;
    } else if (pos_x + bouncer_w >= (float)DISPLAY_WIDTH) {
        pos_x = (float)(DISPLAY_WIDTH - bouncer_w);
        vel_x = -vel_x;
        bounced = true;
    }
    if (pos_y <= 0.0f) {
        pos_y = 0.0f;
        vel_y = -vel_y;
        bounced = true;
    } else if (pos_y + bouncer_h >= (float)DISPLAY_HEIGHT) {
        pos_y = (float)(DISPLAY_HEIGHT - bouncer_h);
        vel_y = -vel_y;
        bounced = true;
    }
    if (bounced) {
        tint_index = (tint_index + 1) % BOUNCER_STYLE_COUNT;
    }
}

static void draw_bouncer_image (void) {
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_tex_blit(bouncer_image, (int)pos_x, (int)pos_y, NULL);
    rdpq_mode_pop();
}

static void draw_bouncer_text (void) {
    rdpq_textparms_t parms = { .style_id = bouncer_styles[tint_index] };
    rdpq_text_print(&parms, FNT_DEFAULT,
                    (int)pos_x, (int)pos_y + BOUNCER_TEXT_H,
                    bouncer_text ? bouncer_text : DEFAULT_TEXT);
}

void screensaver_draw (surface_t *display) {
    if (!display) {
        return;
    }

    rdpq_attach_clear(display, NULL);

    rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
    rdpq_fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    if (active) {
        advance_bouncer(get_ticks_ms());
        if (bouncer_image) {
            draw_bouncer_image();
        } else {
            draw_bouncer_text();
        }
    }

    rdpq_detach_show();
}
