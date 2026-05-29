/**
 * @file apng_decoder.h
 * @brief Minimal APNG decoder — loads animation frames into RGBA16 surfaces.
 * @ingroup menu
 */

#ifndef APNG_DECODER_H__
#define APNG_DECODER_H__

#include <libdragon.h>
#include <stdint.h>

#define APNG_MAX_FRAMES  32

typedef struct {
    surface_t *frame;     /**< Decoded RGBA16 surface (heap-allocated). */
    uint16_t   delay_ms;  /**< Frame duration in milliseconds. */
} apng_frame_t;

typedef struct {
    apng_frame_t frames[APNG_MAX_FRAMES];
    int          frame_count;
    int          width;
    int          height;
} apng_image_t;

/**
 * @brief Load an APNG file.  Falls back gracefully to a single frame for
 *        plain PNGs.  Decodes up to APNG_MAX_FRAMES frames.
 *
 * @param path   Path to the .png/.apng file.
 * @param max_w  Reject images wider than this.
 * @param max_h  Reject images taller than this.
 * @return Allocated apng_image_t on success, NULL on failure.
 */
apng_image_t *apng_load (const char *path, int max_w, int max_h);

void apng_free (apng_image_t *apng);

#endif /* APNG_DECODER_H__ */
