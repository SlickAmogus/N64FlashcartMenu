/**
 * @file background.c
 * @brief Implementation of the background UI component.
 * @ingroup ui_components
 */

#include <stdio.h>
#include <stdlib.h>

#include <mpeg2.h>
#include <yuv.h>

#include "../ui_components.h"
#include "constants.h"
#include "utils/fs.h"

#define CACHE_METADATA_MAGIC    (0x424B4731)

/**
 * @brief Structure representing the background component.
 */
typedef struct {
    char *cache_location;      /**< Path to the cache file location. */
    surface_t *image;          /**< Pointer to the loaded image surface. */
    rspq_block_t *image_display_list; /**< Display list for rendering the image. */
    mpeg2_t *video;            /**< MPEG1 video handle (NULL if not in video mode). */
    yuv_blitter_t video_blitter; /**< YUV blitter for video frame rendering. */
    bool video_has_frame;      /**< True after the first frame has been decoded. */
    uint32_t video_last_frame_ms; /**< Timestamp of last frame advance (ms). */
    uint32_t video_ms_per_frame;  /**< Frame interval in ms derived from framerate. */
} component_background_t;

/**
 * @brief Structure for background image cache metadata.
 */
typedef struct {
    uint32_t magic;    /**< Magic number for cache validation. */
    uint32_t width;    /**< Image width in pixels. */
    uint32_t height;   /**< Image height in pixels. */
    uint32_t size;     /**< Image buffer size in bytes. */
} cache_metadata_t;

static component_background_t *background = NULL;

/**
 * @brief Load background image from cache file if available.
 *
 * @param c Pointer to the background component structure.
 */
static void load_from_cache(component_background_t *c) {
    if (!c->cache_location) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "rb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata;

    if (fread(&cache_metadata, sizeof(cache_metadata), 1, f) != 1) {
        fclose(f);
        return;
    }

    if (cache_metadata.magic != CACHE_METADATA_MAGIC || cache_metadata.width > DISPLAY_WIDTH || cache_metadata.height > DISPLAY_HEIGHT) {
        fclose(f);
        return;
    }

    c->image = calloc(1, sizeof(surface_t));
    *c->image = surface_alloc(FMT_RGBA16, cache_metadata.width, cache_metadata.height);

    if (cache_metadata.size != (c->image->height * c->image->stride)) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
        fclose(f);
        return;
    }

    if (fread(c->image->buffer, cache_metadata.size, 1, f) != 1) {
        surface_free(c->image);
        free(c->image);
        c->image = NULL;
    }

    fclose(f);
}

/**
 * @brief Save background image to cache file.
 *
 * @param c Pointer to the background component structure.
 */
static void save_to_cache(component_background_t *c) {
    if (!c->cache_location || !c->image) {
        return;
    }

    FILE *f;

    if ((f = fopen(c->cache_location, "wb")) == NULL) {
        return;
    }

    cache_metadata_t cache_metadata = {
        .magic = CACHE_METADATA_MAGIC,
        .width = c->image->width,
        .height = c->image->height,
        .size = (c->image->height * c->image->stride),
    };

    fwrite(&cache_metadata, sizeof(cache_metadata), 1, f);
    fwrite(c->image->buffer, cache_metadata.size, 1, f);

    fclose(f);
}

/**
 * @brief Prepare the background image for display (darken and center).
 *
 * @param c Pointer to the background component structure.
 */
static void prepare_background(component_background_t *c) {
    if (!c->image || c->image->width == 0 || c->image->height == 0) {
        return;
    }

    // Darken the image
    rdpq_attach(c->image, NULL);
    rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_set_prim_color(BACKGROUND_OVERLAY_COLOR);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_fill_rectangle(0, 0, c->image->width, c->image->height);
    rdpq_mode_pop();
    rdpq_detach();

    uint16_t image_center_x = (c->image->width / 2);
    uint16_t image_center_y = (c->image->height / 2);

    // Prepare display list
    rspq_block_begin();
    rdpq_mode_push();
        if ((c->image->width != DISPLAY_WIDTH) || (c->image->height != DISPLAY_HEIGHT)) {
            rdpq_set_mode_fill(BACKGROUND_EMPTY_COLOR);
        }
        if (c->image->width != DISPLAY_WIDTH) {
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_CENTER_X - image_center_x,
                DISPLAY_CENTER_Y + image_center_y
            );
            rdpq_fill_rectangle(
                DISPLAY_CENTER_X + image_center_x - (c->image->width % 2),
                DISPLAY_CENTER_Y - image_center_y,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y + image_center_y
            );
        }
        if (c->image->height != DISPLAY_HEIGHT) {
            rdpq_fill_rectangle(
                0,
                0,
                DISPLAY_WIDTH,
                DISPLAY_CENTER_Y - image_center_y
            );
            rdpq_fill_rectangle(
                0,
                DISPLAY_CENTER_Y + image_center_y - (c->image->height % 2),
                DISPLAY_WIDTH,
                DISPLAY_HEIGHT
            );
        }
        rdpq_set_mode_copy(false);
        rdpq_tex_blit(c->image, DISPLAY_CENTER_X - image_center_x, DISPLAY_CENTER_Y - image_center_y, NULL);
    rdpq_mode_pop();
    c->image_display_list = rspq_block_end();
}

/**
 * @brief Free the display list for the background image.
 *
 * @param arg Pointer to the display list (rspq_block_t *).
 */
static void display_list_free(void *arg) {
    rspq_block_free((rspq_block_t *) (arg));
}

/**
 * @brief Initialize the background component and load from cache.
 *
 * @param cache_location Path to the cache file location.
 */
void ui_components_background_init(char *cache_location) {
    if (!background) {
        background = calloc(1, sizeof(component_background_t));
        background->cache_location = strdup(cache_location);
        load_from_cache(background);
        prepare_background(background);
    }
}

/**
 * @brief Initialize the background component for MPEG1 video playback.
 *
 * Opens the given .m1v file and prepares the YUV blitter for per-frame
 * rendering.  The first frame is decoded immediately so the first call to
 * ui_components_background_draw() can display it without delay.
 *
 * Safe to call after ui_components_background_init(); any previously loaded
 * PNG image is left intact (video and PNG paths are mutually exclusive in
 * practice because bg_slideshow_init() only calls this when a video is found).
 *
 * @param path Full filesystem path to the .m1v file.
 */
void ui_components_background_init_video(const char *path) {
    if (!background || !path) {
        return;
    }

    yuv_init();

    mpeg2_t *mp2 = mpeg2_open(path);
    if (!mp2) {
        yuv_close();
        return;
    }

    int w = mpeg2_get_width(mp2);
    int h = mpeg2_get_height(mp2);
    float fps = mpeg2_get_framerate(mp2);

    /* libdragon YUV blitter requires width % 32 == 0 and height % 16 == 0.
     * If the video doesn't meet this, skip it cleanly instead of crashing. */
    if ((w % 32) != 0 || (h % 16) != 0) {
        mpeg2_close(mp2);
        yuv_close();
        return;
    }

    yuv_fmv_parms_t parms = {
        .cs     = &YUV_BT601_TV,
        .halign = YUV_ALIGN_CENTER,
        .valign = YUV_ALIGN_CENTER,
        .zoom   = YUV_ZOOM_FULL,
        .bkg_color = BACKGROUND_EMPTY_COLOR,
    };

    yuv_blitter_t blitter = yuv_blitter_new_fmv(w, h, DISPLAY_WIDTH, DISPLAY_HEIGHT, &parms);

    /* Decode the first frame so draw() can render immediately. */
    bool has_frame = mpeg2_next_frame(mp2);

    background->video             = mp2;
    background->video_blitter     = blitter;
    background->video_has_frame   = has_frame;
    background->video_last_frame_ms = get_ticks_ms();
    background->video_ms_per_frame  = (fps > 0.0f) ? (uint32_t)(1000.0f / fps) : 33;
}

/**
 * @brief Return whether the background component is in video mode.
 *
 * @return true if an MPEG1 video is active, false otherwise.
 */
bool ui_components_background_has_video(void) {
    return background && background->video != NULL;
}

/**
 * @brief Free the background component and its resources.
 */
void ui_components_background_free(void) {
    if (background) {
        if (background->video) {
            yuv_blitter_free(&background->video_blitter);
            mpeg2_close(background->video);
            background->video = NULL;
            yuv_close();
        }
        if (background->image) {
            surface_free(background->image);
            free(background->image);
            background->image = NULL;
        }
        if (background->image_display_list) {
            rdpq_call_deferred(display_list_free, background->image_display_list);
            background->image_display_list = NULL;
        }
        if (background->cache_location) {
            free(background->cache_location);
        }
        free(background);
        background = NULL;
    }
}

/**
 * @brief Replace the background image and update cache/display list.
 *
 * @param image Pointer to the new background image surface.
 */
void ui_components_background_replace_image(surface_t *image) {
    if (!background) {
        return;
    }

    if (background->image) {
        surface_free(background->image);
        free(background->image);
        background->image = NULL;
    }

    if (background->image_display_list) {
        rdpq_call_deferred(display_list_free, background->image_display_list);
        background->image_display_list = NULL;
    }

    background->image = image;
    save_to_cache(background);
    prepare_background(background);
}

/**
 * @brief Draw the background image or clear the screen if not available.
 *
 * In video mode: blits the current decoded frame, applies a darkening
 * overlay, then advances to the next frame when the frame interval elapses.
 * Rewinds to the beginning when the stream ends (looping).
 *
 * In PNG mode: runs the precompiled display list as before.
 */
void ui_components_background_draw(void) {
    if (background && background->video) {
        if (background->video_has_frame) {
            yuv_frame_t frame = mpeg2_get_frame(background->video);
            yuv_blitter_run(&background->video_blitter, &frame);

            /* Darken overlay to match the static PNG appearance. */
            rdpq_mode_push();
                rdpq_set_mode_standard();
                rdpq_set_prim_color(BACKGROUND_OVERLAY_COLOR);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_fill_rectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
            rdpq_mode_pop();
        } else {
            rdpq_clear(BACKGROUND_EMPTY_COLOR);
        }

        /* Advance to the next frame when the interval has elapsed. */
        uint32_t now_ms = get_ticks_ms();
        if (now_ms - background->video_last_frame_ms >= background->video_ms_per_frame) {
            if (!mpeg2_next_frame(background->video)) {
                mpeg2_rewind(background->video);
                background->video_has_frame = mpeg2_next_frame(background->video);
            } else {
                background->video_has_frame = true;
            }
            background->video_last_frame_ms = now_ms;
        }
    } else if (background && background->image_display_list) {
        rspq_block_run(background->image_display_list);
    } else {
        rdpq_clear(BACKGROUND_EMPTY_COLOR);
    }
}
