/**
 * @file gif_decoder.h
 * @brief Minimal GIF decoder — loads up to GIF_MAX_FRAMES animation frames
 *        into RGBA16 surfaces suitable for rdpq_tex_blit.
 * @ingroup menu
 */

#ifndef GIF_DECODER_H__
#define GIF_DECODER_H__

#include <libdragon.h>
#include <stdint.h>

/** Maximum animation frames loaded from a single GIF file. */
#define GIF_MAX_FRAMES  8

typedef struct {
    surface_t *frame;     /**< Decoded RGBA16 surface (heap-allocated). */
    uint16_t   delay_ms;  /**< Frame duration in milliseconds. */
} gif_frame_t;

typedef struct {
    gif_frame_t frames[GIF_MAX_FRAMES];
    int         frame_count;
    int         width;
    int         height;
} gif_image_t;

/**
 * @brief Load a GIF file.  Decodes up to GIF_MAX_FRAMES frames (additional
 *        frames are silently ignored).  Transparent pixels become alpha=0 in
 *        the resulting RGBA16 surfaces.
 *
 * @param path    Path to the .gif file.
 * @param max_w   Reject images wider than this.
 * @param max_h   Reject images taller than this.
 * @return Allocated gif_image_t on success, NULL on failure.
 */
gif_image_t *gif_load (const char *path, int max_w, int max_h);

void gif_free (gif_image_t *gif);

#endif /* GIF_DECODER_H__ */
