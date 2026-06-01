/**
 * @file screensaver.c
 * @brief Idle screensaver — burn-in protection for CRT/plasma displays
 * @ingroup menu
 */

#include <libdragon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apng_decoder.h"
#include "gif_decoder.h"
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

/* ---- Sky / clouds ---- */
#define SKY_CLOUD_IMAGES    3   /* PNG files: cloud1.png .. cloud3.png */
#define SKY_CLOUD_COUNT     6   /* instances (reuse images) */

typedef struct { float x, y, speed; int img; } cloud_t;

static surface_t *cloud_surfaces[SKY_CLOUD_IMAGES];
static int        cloud_surf_count = 0;
static cloud_t    clouds[SKY_CLOUD_COUNT];
static bool       clouds_ready    = false;
static uint32_t   cloud_last_ms   = 0;

/* ---- Rain ---- */
#define RAIN_COUNT      80
#define RAIN_FAST_N     30   /* bright white, fastest */
#define RAIN_MED_N      30   /* blue-white, medium */
#define RAIN_SLOW_N     20   /* dim grey-blue, slowest */
/* Horizontal wind drift in px/ms.  Negative = leftward. Applied
 * uniformly so slower drops drift further per pixel of fall, mimicking
 * the angle real rain takes in a steady breeze. */
#define RAIN_WIND_PXMS  (-0.06f)

typedef struct { float x, y; float speed; int len; } rain_drop_t;

static rain_drop_t rain_drops[RAIN_COUNT];
static bool        rain_ready   = false;
static uint32_t    rain_last_ms = 0;

static void init_rain (void) {
    for (int i = 0; i < RAIN_COUNT; i++) {
        rain_drops[i].x = (float)(rand() % DISPLAY_WIDTH);
        rain_drops[i].y = (float)(rand() % DISPLAY_HEIGHT);
        if (i < RAIN_FAST_N) {
            rain_drops[i].speed = 0.50f + (float)(rand() % 15) * 0.01f;
            rain_drops[i].len   = 5 + rand() % 4;
        } else if (i < RAIN_FAST_N + RAIN_MED_N) {
            rain_drops[i].speed = 0.28f + (float)(rand() % 15) * 0.01f;
            rain_drops[i].len   = 4 + rand() % 3;
        } else {
            rain_drops[i].speed = 0.13f + (float)(rand() % 10) * 0.01f;
            rain_drops[i].len   = 2 + rand() % 3;
        }
    }
    rain_ready   = true;
    rain_last_ms = 0;
}

static void advance_rain (uint32_t now_ms) {
    if (!rain_ready) { init_rain(); rain_last_ms = now_ms; return; }
    uint32_t dt = (rain_last_ms == 0) ? 16 : (now_ms - rain_last_ms);
    if (dt > 100) dt = 100;
    rain_last_ms = now_ms;
    float wind = RAIN_WIND_PXMS * (float)dt;
    for (int i = 0; i < RAIN_COUNT; i++) {
        rain_drops[i].y += rain_drops[i].speed * (float)dt;
        rain_drops[i].x += wind;
        if (rain_drops[i].y > (float)(DISPLAY_HEIGHT + rain_drops[i].len)) {
            rain_drops[i].y = -(float)(rain_drops[i].len);
            rain_drops[i].x = (float)(rand() % DISPLAY_WIDTH);
        }
        /* Wrap horizontally so the wind doesn't drain one edge of the screen. */
        if (rain_drops[i].x < 0.0f)                       rain_drops[i].x += (float)DISPLAY_WIDTH;
        else if (rain_drops[i].x >= (float)DISPLAY_WIDTH) rain_drops[i].x -= (float)DISPLAY_WIDTH;
    }
}

static void draw_rain (void) {
    rdpq_set_mode_fill(RGBA32(220, 235, 255, 255));
    for (int i = 0; i < RAIN_FAST_N; i++) {
        int rx = (int)rain_drops[i].x;
        int ry = (int)rain_drops[i].y;
        int ry2 = ry + rain_drops[i].len;
        if (ry < DISPLAY_HEIGHT && ry2 > 0)
            rdpq_fill_rectangle(rx, (ry < 0 ? 0 : ry), rx + 1, (ry2 > DISPLAY_HEIGHT ? DISPLAY_HEIGHT : ry2));
    }
    rdpq_set_mode_fill(RGBA32(140, 170, 210, 255));
    for (int i = RAIN_FAST_N; i < RAIN_FAST_N + RAIN_MED_N; i++) {
        int rx = (int)rain_drops[i].x;
        int ry = (int)rain_drops[i].y;
        int ry2 = ry + rain_drops[i].len;
        if (ry < DISPLAY_HEIGHT && ry2 > 0)
            rdpq_fill_rectangle(rx, (ry < 0 ? 0 : ry), rx + 1, (ry2 > DISPLAY_HEIGHT ? DISPLAY_HEIGHT : ry2));
    }
    rdpq_set_mode_fill(RGBA32(60, 80, 110, 255));
    for (int i = RAIN_FAST_N + RAIN_MED_N; i < RAIN_COUNT; i++) {
        int rx = (int)rain_drops[i].x;
        int ry = (int)rain_drops[i].y;
        int ry2 = ry + rain_drops[i].len;
        if (ry < DISPLAY_HEIGHT && ry2 > 0)
            rdpq_fill_rectangle(rx, (ry < 0 ? 0 : ry), rx + 1, (ry2 > DISPLAY_HEIGHT ? DISPLAY_HEIGHT : ry2));
    }
}

/* ---- Fire ---- */
#define FIRE_COUNT      100
#define FIRE_MAX_RISE   360  /* pixels particles can rise before resetting */

typedef struct { float x, y, dx, dy; } fire_particle_t;

static fire_particle_t fire_particles[FIRE_COUNT];
static bool            fire_ready   = false;
static uint32_t        fire_last_ms = 0;

static void init_fire (void) {
    for (int i = 0; i < FIRE_COUNT; i++) {
        fire_particles[i].x  = (float)(rand() % DISPLAY_WIDTH);
        fire_particles[i].y  = (float)(DISPLAY_HEIGHT - rand() % FIRE_MAX_RISE);
        fire_particles[i].dx = ((float)(rand() % 21) - 10) * 0.004f;
        fire_particles[i].dy = -0.25f - (float)(rand() % 50) * 0.008f;
    }
    fire_ready   = true;
    fire_last_ms = 0;
}

static void advance_fire (uint32_t now_ms) {
    if (!fire_ready) { init_fire(); fire_last_ms = now_ms; return; }
    uint32_t dt = (fire_last_ms == 0) ? 16 : (now_ms - fire_last_ms);
    if (dt > 100) dt = 100;
    fire_last_ms = now_ms;
    float fdt = (float)dt;
    for (int i = 0; i < FIRE_COUNT; i++) {
        fire_particles[i].x += fire_particles[i].dx * fdt;
        fire_particles[i].y += fire_particles[i].dy * fdt;
        /* Random horizontal wobble for flickering effect */
        fire_particles[i].dx += ((float)(rand() % 5) - 2) * 0.0003f;
        if (fire_particles[i].dx >  0.05f) fire_particles[i].dx =  0.05f;
        if (fire_particles[i].dx < -0.05f) fire_particles[i].dx = -0.05f;
        if (fire_particles[i].y < (float)(DISPLAY_HEIGHT - FIRE_MAX_RISE)) {
            fire_particles[i].x  = (float)(rand() % DISPLAY_WIDTH);
            fire_particles[i].y  = (float)(DISPLAY_HEIGHT - rand() % 20);
            fire_particles[i].dx = ((float)(rand() % 21) - 10) * 0.004f;
            fire_particles[i].dy = -0.25f - (float)(rand() % 50) * 0.008f;
        }
    }
}

static void draw_fire (void) {
    /* Hot-core glow at the very base — two thin strips, brighter inner,
     * suggests a bed of incandescent coals the particles rise from. */
    rdpq_set_mode_fill(RGBA32(120, 25, 0, 255));
    rdpq_fill_rectangle(0, DISPLAY_HEIGHT - 6, DISPLAY_WIDTH, DISPLAY_HEIGHT - 3);
    rdpq_set_mode_fill(RGBA32(255, 210, 130, 255));
    rdpq_fill_rectangle(0, DISPLAY_HEIGHT - 3, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* 5 color tiers from hottest (bottom) to coolest (top).
     * Each tier covers a distinct Y band so particles are drawn exactly once. */
    static const int     thresholds[] = { 50, 120, 210, 310, FIRE_MAX_RISE };
    static const color_t colors[]     = {
        RGBA32(255, 250, 160, 255),  /* near base: white-yellow */
        RGBA32(255, 140,  15, 255),  /* orange */
        RGBA32(255,  55,   0, 255),  /* orange-red */
        RGBA32(180,  10,   0, 255),  /* red */
        RGBA32( 65,   0,   0, 255),  /* dark red / dying */
    };
    for (int t = 0; t < 5; t++) {
        int y_min = DISPLAY_HEIGHT - thresholds[t];
        int y_max = (t == 0) ? DISPLAY_HEIGHT : DISPLAY_HEIGHT - thresholds[t - 1];
        rdpq_set_mode_fill(colors[t]);
        for (int i = 0; i < FIRE_COUNT; i++) {
            int py = (int)fire_particles[i].y;
            if (py >= y_min && py < y_max) {
                int px = (int)fire_particles[i].x;
                if (px >= 0 && px < DISPLAY_WIDTH - 1)
                    rdpq_fill_rectangle(px, py, px + 2, py + 3);
            }
        }
    }
}

/* ---- Starfield ---- */
#define STAR_COUNT      240
#define STAR_FAST_N     80      /* bright white,  fastest */
#define STAR_MED_N      80      /* light grey, medium */
#define STAR_SLOW_N     80      /* dim grey,   slowest */
#define STAR_FAST_SPD   0.060f  /* px/ms */
#define STAR_MED_SPD    0.030f
#define STAR_SLOW_SPD   0.012f

typedef struct { float x, y; } star_pos_t;

static star_pos_t stars[STAR_COUNT];
static bool       stars_ready  = false;
static uint32_t   star_last_ms = 0;

/* ---- Background ---- */
static int current_bg = SCREENSAVER_BG_BLACK;

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

static apng_image_t *bouncer_apng     = NULL;
static int           apng_frame_idx  = 0;
static uint32_t      apng_last_ms    = 0;

static gif_image_t *bouncer_gif      = NULL;
static int          gif_frame_idx    = 0;
static uint32_t     gif_last_ms      = 0;

static int bouncer_w = 0;
static int bouncer_h = 0;

static float pos_x, pos_y;
static float vel_x, vel_y;
static int   tint_index = 0;
static uint32_t last_frame_ms = 0;


static void init_stars (void) {
    for (int i = 0; i < STAR_COUNT; i++) {
        stars[i].x = (float)(rand() % DISPLAY_WIDTH);
        stars[i].y = (float)(rand() % DISPLAY_HEIGHT);
    }
    stars_ready  = true;
    star_last_ms = 0;
}

static void advance_stars (uint32_t now_ms) {
    if (!stars_ready) {
        init_stars();
        star_last_ms = now_ms;
        return;
    }
    uint32_t dt = (star_last_ms == 0) ? 16 : (now_ms - star_last_ms);
    if (dt > 100) dt = 100;
    star_last_ms = now_ms;

    for (int i = 0; i < STAR_FAST_N; i++) {
        stars[i].x -= STAR_FAST_SPD * (float)dt;
        if (stars[i].x < 0.0f) stars[i].x += (float)DISPLAY_WIDTH;
    }
    for (int i = STAR_FAST_N; i < STAR_FAST_N + STAR_MED_N; i++) {
        stars[i].x -= STAR_MED_SPD * (float)dt;
        if (stars[i].x < 0.0f) stars[i].x += (float)DISPLAY_WIDTH;
    }
    for (int i = STAR_FAST_N + STAR_MED_N; i < STAR_COUNT; i++) {
        stars[i].x -= STAR_SLOW_SPD * (float)dt;
        if (stars[i].x < 0.0f) stars[i].x += (float)DISPLAY_WIDTH;
    }
}

static void draw_stars (void) {
    rdpq_set_mode_fill(RGBA32(255, 255, 255, 255));
    for (int i = 0; i < STAR_FAST_N; i++) {
        int sx = (int)stars[i].x, sy = (int)stars[i].y;
        rdpq_fill_rectangle(sx, sy, sx + 1, sy + 1);
    }
    rdpq_set_mode_fill(RGBA32(160, 160, 160, 255));
    for (int i = STAR_FAST_N; i < STAR_FAST_N + STAR_MED_N; i++) {
        int sx = (int)stars[i].x, sy = (int)stars[i].y;
        rdpq_fill_rectangle(sx, sy, sx + 1, sy + 1);
    }
    rdpq_set_mode_fill(RGBA32(80, 80, 80, 255));
    for (int i = STAR_FAST_N + STAR_MED_N; i < STAR_COUNT; i++) {
        int sx = (int)stars[i].x, sy = (int)stars[i].y;
        rdpq_fill_rectangle(sx, sy, sx + 1, sy + 1);
    }
}

static color_t get_bg_color (int bg) {
    switch (bg) {
        case SCREENSAVER_BG_NAVY:     return RGBA32(0,   0,  64, 255);
        case SCREENSAVER_BG_CYAN:     return RGBA32(0,  48,  80, 255);
        case SCREENSAVER_BG_PURPLE:   return RGBA32(32,  0,  64, 255);
        case SCREENSAVER_BG_RED:      return RGBA32(64,  0,   0, 255);
        case SCREENSAVER_BG_GREEN:    return RGBA32(0,  48,   0, 255);
        case SCREENSAVER_BG_SKY:      return RGBA32(55, 110, 160, 255);
        case SCREENSAVER_BG_RAIN:     return RGBA32(8,   12,  25, 255);
        case SCREENSAVER_BG_FIRE:     return RGBA32(0,    0,   0, 255);
        default:                      return RGBA32(0,   0,   0, 255);
    }
}

/* Draw a vertical gradient as a stack of solid-color strips.  Cheap on
 * RDP — one rectangle per strip — and 24 strips is enough that the
 * banding is invisible on N64 composite/RGB output. */
#define GRADIENT_STRIPS  24
static void draw_vertical_gradient (int r0, int g0, int b0, int r1, int g1, int b1) {
    int strip_h = DISPLAY_HEIGHT / GRADIENT_STRIPS;
    for (int i = 0; i < GRADIENT_STRIPS; i++) {
        float t = (float)i / (float)(GRADIENT_STRIPS - 1);
        int r = (int)((float)r0 * (1.0f - t) + (float)r1 * t);
        int g = (int)((float)g0 * (1.0f - t) + (float)g1 * t);
        int b = (int)((float)b0 * (1.0f - t) + (float)b1 * t);
        int y0 = i * strip_h;
        int y1 = (i == GRADIENT_STRIPS - 1) ? DISPLAY_HEIGHT : (i + 1) * strip_h;
        rdpq_set_mode_fill(RGBA32(r, g, b, 255));
        rdpq_fill_rectangle(0, y0, DISPLAY_WIDTH, y1);
    }
}

/* Per-background fill — gradient for the atmospheric BGs, solid for the rest. */
static void draw_bg_fill (int bg) {
    switch (bg) {
        case SCREENSAVER_BG_SKY:
            /* Deeper blue overhead, brighter near the horizon. */
            draw_vertical_gradient(35, 90, 145,  120, 180, 215);
            break;
        case SCREENSAVER_BG_RAIN:
            /* Stormy night — slate-blue top fading to near-black ground. */
            draw_vertical_gradient(20, 35, 60,  3, 6, 14);
            break;
        case SCREENSAVER_BG_FIRE:
            /* Almost black sky above, deepening to ember-red along the base. */
            draw_vertical_gradient(0, 0, 0,  55, 5, 0);
            break;
        default:
            rdpq_set_mode_fill(get_bg_color(bg));
            rdpq_fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            break;
    }
}

/* ---- Cloud advance / draw ----------------------------------------- */

static void init_clouds (void) {
    /* Speed tiers: fast/medium/slow, 2 instances each */
    static const float speeds[SKY_CLOUD_COUNT] = {
        0.020f, 0.018f,  /* fast   */
        0.012f, 0.010f,  /* medium */
        0.007f, 0.005f,  /* slow   */
    };
    /* Each tier gets its own vertical band so clouds don't clump at the top.
     * Band height = (DISPLAY_HEIGHT * 3/5) / 3 = 96 pixels each. */
    static const int y_band_min[SKY_CLOUD_COUNT] = {  0,   0,  96,  96, 192, 192 };
    static const int y_band_max[SKY_CLOUD_COUNT] = { 96,  96, 192, 192, 288, 288 };

    int n = cloud_surf_count > 0 ? cloud_surf_count : 1;
    /* Divide the screen width into equal slots so clouds start spread out. */
    int slot_w = DISPLAY_WIDTH / SKY_CLOUD_COUNT;
    for (int i = 0; i < SKY_CLOUD_COUNT; i++) {
        int y_range = y_band_max[i] - y_band_min[i];
        clouds[i].x     = (float)(i * slot_w + rand() % slot_w);
        clouds[i].y     = (float)(y_band_min[i] + rand() % y_range);
        clouds[i].speed = speeds[i];
        clouds[i].img   = i % n;
    }
    clouds_ready  = true;
    cloud_last_ms = 0;
}

static void advance_clouds (uint32_t now_ms) {
    if (!clouds_ready) {
        init_clouds();
        cloud_last_ms = now_ms;
        return;
    }
    uint32_t dt = (cloud_last_ms == 0) ? 16 : (now_ms - cloud_last_ms);
    if (dt > 100) dt = 100;
    cloud_last_ms = now_ms;

    for (int i = 0; i < SKY_CLOUD_COUNT; i++) {
        if (cloud_surf_count == 0) break;
        surface_t *surf = cloud_surfaces[clouds[i].img];
        int cw = surf ? surf->width : 128;

        clouds[i].x -= clouds[i].speed * (float)dt;
        if (clouds[i].x + (float)cw < 0.0f) {
            clouds[i].x = (float)DISPLAY_WIDTH + (float)(rand() % 80);
            clouds[i].y = (float)(rand() % (DISPLAY_HEIGHT * 3 / 5));
        }
    }
}

/* Draw a surface with a subtle cool-grey tint (TEX * ENV) to soften pure
 * white clouds against the sky.  Alpha blending is preserved via TEX alpha. */
static void draw_surface_at (surface_t *surf, int x, int y) {
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_set_env_color(RGBA32(205, 215, 225, 255));
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0, 0, ENV, 0), (0, 0, 0, TEX0)));
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        rdpq_tex_blit(surf, x, y, NULL);
    rdpq_mode_pop();
}

static void draw_clouds (void) {
    for (int i = 0; i < SKY_CLOUD_COUNT; i++) {
        if (clouds[i].img >= cloud_surf_count) continue;
        surface_t *surf = cloud_surfaces[clouds[i].img];
        if (surf) draw_surface_at(surf, (int)clouds[i].x, (int)clouds[i].y);
    }
}

void screensaver_set_bg (int bg) {
    if (bg < 0 || bg >= SCREENSAVER_BG_COUNT) bg = SCREENSAVER_BG_BLACK;
    current_bg = bg;
    if (bg == SCREENSAVER_BG_STARFIELD && !stars_ready) {
        init_stars();
    }
    if (bg == SCREENSAVER_BG_SKY) {
        clouds_ready  = false;
        cloud_last_ms = 0;
    }
    if (bg == SCREENSAVER_BG_RAIN) {
        rain_ready   = false;
        rain_last_ms = 0;
    }
    if (bg == SCREENSAVER_BG_FIRE) {
        fire_ready   = false;
        fire_last_ms = 0;
    }
}

static float random_velocity_component (void) {
    return (rand() & 1) ? BOUNCER_SPEED_PXMS : -BOUNCER_SPEED_PXMS;
}

static void recompute_dimensions (void) {
    if (bouncer_apng && bouncer_apng->frame_count > 0 && bouncer_apng->frames[0].frame) {
        bouncer_w = bouncer_apng->frames[0].frame->width;
        bouncer_h = bouncer_apng->frames[0].frame->height;
    } else if (bouncer_gif && bouncer_gif->frame_count > 0 && bouncer_gif->frames[0].frame) {
        bouncer_w = bouncer_gif->frames[0].frame->width;
        bouncer_h = bouncer_gif->frames[0].frame->height;
    } else if (bouncer_image && bouncer_image->width > 0 && bouncer_image->height > 0) {
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


void screensaver_init (const char *image_dir, const char *text) {
    active        = false;
    pos_x         = 0.0f;
    pos_y         = 0.0f;
    vel_x         = BOUNCER_SPEED_PXMS;
    vel_y         = BOUNCER_SPEED_PXMS;
    tint_index    = 0;
    last_frame_ms = 0;

    screensaver_set_text(text);

    /* Load cloud images for the Sky background */
    cloud_surf_count = 0;
    if (image_dir && image_dir[0]) {
        char buf[300];
        for (int ci = 0; ci < SKY_CLOUD_IMAGES; ci++) {
            snprintf(buf, sizeof(buf), "%s/cloud%d.png", image_dir, ci + 1);
            apng_image_t *a = apng_load(buf, MAX_BOUNCER_PNG_W, MAX_BOUNCER_PNG_H);
            if (a && a->frame_count > 0 && a->frames[0].frame) {
                cloud_surfaces[cloud_surf_count] = a->frames[0].frame;
                a->frames[0].frame = NULL;
                apng_free(a);
                cloud_surf_count++;
            } else if (a) {
                apng_free(a);
            }
        }
    }

    if (image_dir && image_dir[0]) {
        char buf[300];
        /* 1) Try APNG */
        snprintf(buf, sizeof(buf), "%s/bouncer.apng", image_dir);
        bouncer_apng = apng_load(buf, MAX_BOUNCER_PNG_W, MAX_BOUNCER_PNG_H);
        if (bouncer_apng) {
            apng_frame_idx = 0;
            apng_last_ms   = 0;
            recompute_dimensions();
        } else {
            /* 2) Try animated GIF */
            snprintf(buf, sizeof(buf), "%s/bouncer.gif", image_dir);
            bouncer_gif = gif_load(buf, MAX_BOUNCER_PNG_W, MAX_BOUNCER_PNG_H);
            if (bouncer_gif) {
                gif_frame_idx = 0;
                gif_last_ms   = 0;
                recompute_dimensions();
            } else {
                /* 3) Fall back to static PNG (loaded asynchronously) */
                snprintf(buf, sizeof(buf), "%s/bouncer.png", image_dir);
                if (pending_image_path) free(pending_image_path);
                pending_image_path = strdup(buf);
                try_start_image_load();
            }
        }
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
    image_pending  = false;
    stars_ready    = false;
    current_bg     = SCREENSAVER_BG_BLACK;
    if (bouncer_apng) {
        apng_free(bouncer_apng);
        bouncer_apng = NULL;
    }
    apng_frame_idx = 0;
    apng_last_ms   = 0;
    if (bouncer_gif) {
        gif_free(bouncer_gif);
        bouncer_gif = NULL;
    }
    gif_frame_idx = 0;
    gif_last_ms   = 0;
    for (int i = 0; i < cloud_surf_count; i++) {
        if (cloud_surfaces[i]) {
            surface_free(cloud_surfaces[i]);
            free(cloud_surfaces[i]);
            cloud_surfaces[i] = NULL;
        }
    }
    cloud_surf_count = 0;
    clouds_ready     = false;
    cloud_last_ms    = 0;
    rain_ready       = false;
    rain_last_ms     = 0;
    fire_ready       = false;
    fire_last_ms     = 0;
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
        tint_index     = rand() % BOUNCER_STYLE_COUNT;
        last_frame_ms  = get_ticks_ms();
        apng_frame_idx = 0;
        apng_last_ms   = 0;
        gif_frame_idx  = 0;
        gif_last_ms    = 0;
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

static void draw_bouncer_surface (surface_t *surf) {
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_TEX);
        /* Standard alpha-over: src_rgb * src_alpha + dst_rgb * (1-src_alpha) */
        rdpq_mode_blender(RDPQ_BLENDER((IN_RGB, IN_ALPHA, MEMORY_RGB, INV_MUX_ALPHA)));
        rdpq_tex_blit(surf, (int)pos_x, (int)pos_y, NULL);
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

    uint32_t now_ms = get_ticks_ms();

    /* Background fill — flat colour or vertical gradient depending on bg. */
    draw_bg_fill(current_bg);

    if (current_bg == SCREENSAVER_BG_STARFIELD) {
        advance_stars(now_ms);
        draw_stars();
    } else if (current_bg == SCREENSAVER_BG_SKY) {
        advance_clouds(now_ms);
        draw_clouds();
    } else if (current_bg == SCREENSAVER_BG_RAIN) {
        advance_rain(now_ms);
        draw_rain();
    } else if (current_bg == SCREENSAVER_BG_FIRE) {
        advance_fire(now_ms);
        draw_fire();
    }

    if (active) {
        advance_bouncer(now_ms);
        if (bouncer_apng) {
            /* Advance APNG animation frame */
            if (apng_last_ms == 0) {
                apng_last_ms = now_ms;
            } else if (now_ms - apng_last_ms >= bouncer_apng->frames[apng_frame_idx].delay_ms) {
                apng_frame_idx = (apng_frame_idx + 1) % bouncer_apng->frame_count;
                apng_last_ms   = now_ms;
            }
            draw_bouncer_surface(bouncer_apng->frames[apng_frame_idx].frame);
        } else if (bouncer_gif) {
            /* Advance GIF animation frame */
            if (gif_last_ms == 0) {
                gif_last_ms = now_ms;
            } else if (now_ms - gif_last_ms >= bouncer_gif->frames[gif_frame_idx].delay_ms) {
                gif_frame_idx = (gif_frame_idx + 1) % bouncer_gif->frame_count;
                gif_last_ms   = now_ms;
            }
            draw_bouncer_surface(bouncer_gif->frames[gif_frame_idx].frame);
        } else if (bouncer_image) {
            draw_bouncer_surface(bouncer_image);
        } else {
            draw_bouncer_text();
        }
    }

    rdpq_detach_show();
}
