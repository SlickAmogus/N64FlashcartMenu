/**
 * @file bg_slideshow.c
 * @brief Background image slideshow implementation
 * @ingroup menu
 */

#include <libdragon.h>
#include <string.h>
#include <stdlib.h>

#include "bg_slideshow.h"
#include "png_decoder.h"
#include "ui_components.h"
#include "ui_components/constants.h"

#define MAX_BG_FILES    20

static char *bg_files[MAX_BG_FILES];
static int bg_order[MAX_BG_FILES];
static int bg_count = 0;
static int bg_current = 0;

static int bg_interval_secs = 0;
static uint32_t bg_last_change_ms = 0;
static bool bg_change_pending = false;
static bool bg_decoding = false;
static bool bg_initial_pending = false;

static char *bg_dir_saved = NULL;

static void bg_shuffle(void) {
    for (int i = bg_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = bg_order[i];
        bg_order[i] = bg_order[j];
        bg_order[j] = tmp;
    }
    bg_current = 0;
}

static void bg_png_callback(png_err_t err, surface_t *image, void *data) {
    (void)data;
    bg_decoding = false;
    if (err == PNG_OK && image) {
        ui_components_background_replace_image(image);
    } else if (image) {
        surface_free(image);
        free(image);
    }
}

void bg_slideshow_init(const char *backgrounds_dir, bool allow_video) {
    bg_count = 0;

    if (!backgrounds_dir) {
        return;
    }

    if (bg_dir_saved) {
        free(bg_dir_saved);
    }
    bg_dir_saved = strdup(backgrounds_dir);

    dir_t dir;
    char path_buf[512];
    bool video_found = false;

    if (dir_findfirst(backgrounds_dir, &dir) != 0) {
        return;
    }

    do {
        if (dir.d_type != DT_REG) {
            continue;
        }

        char *dot = strrchr(dir.d_name, '.');
        if (!dot) {
            continue;
        }

        if (allow_video && !video_found && strcasecmp(dot + 1, "m1v") == 0) {
            snprintf(path_buf, sizeof(path_buf), "%s/%s", backgrounds_dir, dir.d_name);
            ui_components_background_init_video(path_buf);
            video_found = true;
        } else if (!video_found && strcasecmp(dot + 1, "png") == 0) {
            if (bg_count >= MAX_BG_FILES) {
                continue;
            }
            snprintf(path_buf, sizeof(path_buf), "%s/%s", backgrounds_dir, dir.d_name);
            bg_files[bg_count] = strdup(path_buf);
            bg_order[bg_count] = bg_count;
            bg_count++;
        }
    } while (dir_findnext(backgrounds_dir, &dir) == 0);

    if (video_found) {
        /* Video mode — free any PNG entries accumulated before video was found */
        for (int i = 0; i < bg_count; i++) {
            free(bg_files[i]);
            bg_files[i] = NULL;
        }
        bg_count = 0;
        return;
    }

    if (bg_count > 1) {
        srand((unsigned int)timer_ticks());
        bg_shuffle();
    }

    if (bg_count > 0) {
        /* Load the first background immediately; if the decoder is busy at
         * init time, mark it pending so bg_slideshow_process() retries. */
        if (png_decoder_start(bg_files[bg_order[0]], DISPLAY_WIDTH, DISPLAY_HEIGHT,
                              bg_png_callback, NULL) == PNG_OK) {
            bg_decoding = true;
        } else {
            bg_initial_pending = true;
        }
        bg_last_change_ms = get_ticks_ms();
    }
}

void bg_slideshow_deinit(void) {
    for (int i = 0; i < bg_count; i++) {
        free(bg_files[i]);
        bg_files[i] = NULL;
    }
    bg_count = 0;
    bg_decoding = false;
    bg_change_pending = false;
    bg_initial_pending = false;
}

void bg_slideshow_reinit(bool allow_video) {
    if (!bg_dir_saved) {
        return;
    }
    /* Release any active video so background_draw() stops using it. */
    if (ui_components_background_has_video()) {
        ui_components_background_free_video();
    }
    bg_slideshow_deinit();
    bg_slideshow_init(bg_dir_saved, allow_video);
}

void bg_slideshow_set_interval(int seconds) {
    bg_interval_secs = seconds;
    bg_last_change_ms = get_ticks_ms();
    bg_change_pending = false;
}

void bg_slideshow_process(void) {
    if (bg_decoding) {
        return;
    }

    /* Retry the initial load if it was deferred because the decoder was busy
     * at init time.  This runs regardless of bg_count so a single PNG works. */
    if (bg_initial_pending && bg_count > 0) {
        if (png_decoder_start(bg_files[bg_order[0]], DISPLAY_WIDTH, DISPLAY_HEIGHT,
                              bg_png_callback, NULL) == PNG_OK) {
            bg_initial_pending = false;
            bg_decoding = true;
        }
        return;
    }

    if (bg_interval_secs == 0 || bg_count <= 1) {
        return;
    }

    uint32_t now_ms = get_ticks_ms();
    uint32_t interval_ms = (uint32_t)bg_interval_secs * 1000;

    if (!bg_change_pending && (now_ms - bg_last_change_ms >= interval_ms)) {
        bg_change_pending = true;
    }

    if (bg_change_pending) {
        bg_current = (bg_current + 1) % bg_count;
        if (bg_current == 0) {
            bg_shuffle();
        }

        png_err_t err = png_decoder_start(
            bg_files[bg_order[bg_current]],
            DISPLAY_WIDTH, DISPLAY_HEIGHT,
            bg_png_callback, NULL);

        if (err == PNG_OK) {
            bg_change_pending = false;
            bg_decoding = true;
            bg_last_change_ms = now_ms;
        }
        /* If PNG_ERR_BUSY the decoder is occupied (e.g. loading boxart).
         * Keep pending = true and retry next frame. */
    }
}
