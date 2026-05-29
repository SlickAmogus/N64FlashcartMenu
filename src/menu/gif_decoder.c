/**
 * @file gif_decoder.c
 * @brief Minimal GIF 87a/89a decoder.
 * @ingroup menu
 *
 * Supports: global/local colour tables, transparency, animation (GCE delays,
 * disposal modes 0-3).  Interlaced frames are decoded as non-interlaced
 * (row order will look scrambled but the data is still safe).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>

#include "gif_decoder.h"

/* ----------------------------------------------------------------
 * LZW constants
 * ---------------------------------------------------------------- */
#define LZW_MAX_CODES   4096
#define LZW_STACK_SIZE  LZW_MAX_CODES

/* ----------------------------------------------------------------
 * Bit-stream reader  (GIF/LZW is LSB-first)
 * ---------------------------------------------------------------- */
typedef struct {
    const uint8_t *data;
    int            size;
    int            pos;
    uint32_t       buf;
    int            nbits;
} bs_t;

static int bs_read (bs_t *bs, int n) {
    while (bs->nbits < n) {
        if (bs->pos >= bs->size) return -1;
        bs->buf |= (uint32_t)bs->data[bs->pos++] << bs->nbits;
        bs->nbits += 8;
    }
    int v = (int)(bs->buf & ((1u << n) - 1));
    bs->buf   >>= n;
    bs->nbits  -= n;
    return v;
}

/* ----------------------------------------------------------------
 * GIF/LZW decompressor
 * Returns pixel count written to out, or -1 on error.
 * ---------------------------------------------------------------- */
static int lzw_decompress (const uint8_t *data, int data_len,
                           uint8_t *out, int out_cap, int min_lzw) {
    int clear = 1 << min_lzw;
    int eoi   = clear + 1;
    bs_t bs   = { data, data_len, 0, 0, 0 };

    uint16_t prefix[LZW_MAX_CODES];
    uint8_t  suffix[LZW_MAX_CODES];
    uint8_t  stack[LZW_STACK_SIZE];

    int code_size = min_lzw + 1;
    int next_code = eoi + 1;
    int prev      = -1;
    int first     = 0;
    int out_pos   = 0;

    for (int i = 0; i < clear; i++) {
        prefix[i] = (uint16_t)LZW_MAX_CODES;
        suffix[i] = (uint8_t)i;
    }

    for (;;) {
        int code = bs_read(&bs, code_size);
        if (code < 0) break;

        if (code == clear) {
            code_size = min_lzw + 1;
            next_code = eoi + 1;
            prev      = -1;
            continue;
        }
        if (code == eoi) break;

        int sp = 0, c = code;

        if (code == next_code) {
            if (prev < 0) return -1;
            stack[sp++] = (uint8_t)first;   /* appended copy of prev's first byte */
            c = prev;
        } else if (code > next_code) {
            return -1;
        }

        /* Walk the prefix chain; guard against corrupt infinite loops */
        int guard = LZW_MAX_CODES;
        while (c > eoi && guard-- > 0) {
            if (sp >= LZW_STACK_SIZE) return -1;
            stack[sp++] = suffix[c];
            c = (int)prefix[c];
        }
        if (guard < 0) return -1;

        first = c;                      /* base pixel value = first byte of string */
        stack[sp++] = (uint8_t)c;

        /* Add new entry: prev_string + first_byte_of_current_string */
        if (prev >= 0 && next_code < LZW_MAX_CODES) {
            prefix[next_code] = (uint16_t)prev;
            suffix[next_code] = (uint8_t)first;
            next_code++;
            if (next_code == (1 << code_size) && code_size < 12)
                code_size++;
        }

        /* Output stack in forward order (stack built in reverse) */
        while (sp > 0 && out_pos < out_cap)
            out[out_pos++] = stack[--sp];

        prev = code;
    }
    return out_pos;
}

/* ----------------------------------------------------------------
 * Sub-block helpers
 * ---------------------------------------------------------------- */
static bool skip_sub_blocks (FILE *f) {
    uint8_t n;
    while (fread(&n, 1, 1, f) == 1) {
        if (n == 0) return true;
        if (fseek(f, n, SEEK_CUR) != 0) return false;
    }
    return false;
}

static uint8_t *read_sub_blocks (FILE *f, int *out_len) {
    int     cap  = 4096;
    int     used = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) return NULL;

    uint8_t n;
    while (fread(&n, 1, 1, f) == 1) {
        if (n == 0) break;
        if (used + n > cap) {
            cap = (used + n) * 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        if ((int)fread(buf + used, 1, n, f) != n) { free(buf); return NULL; }
        used += n;
    }
    *out_len = used;
    return buf;
}

/* ----------------------------------------------------------------
 * gif_load
 * ---------------------------------------------------------------- */
gif_image_t *gif_load (const char *path, int max_w, int max_h) {
    FILE        *f           = NULL;
    gif_image_t *gif         = NULL;
    uint16_t    *canvas      = NULL;
    uint16_t    *prev_canvas = NULL;
    uint8_t     *indices     = NULL;

    f = fopen(path, "rb");
    if (!f) return NULL;

    gif = calloc(1, sizeof(gif_image_t));
    if (!gif) goto fail;

    /* Header */
    uint8_t hdr[6];
    if (fread(hdr, 1, 6, f) != 6) goto fail;
    if (memcmp(hdr, "GIF87a", 6) != 0 && memcmp(hdr, "GIF89a", 6) != 0) goto fail;

    /* Logical Screen Descriptor */
    uint8_t lsd[7];
    if (fread(lsd, 1, 7, f) != 7) goto fail;

    int cw = lsd[0] | (lsd[1] << 8);   /* canvas width  */
    int ch = lsd[2] | (lsd[3] << 8);   /* canvas height */
    if (cw <= 0 || ch <= 0 || cw > max_w || ch > max_h) goto fail;

    gif->width  = cw;
    gif->height = ch;

    int has_gct  = (lsd[4] >> 7) & 1;
    int gct_size = 1 << ((lsd[4] & 7) + 1);
    int bg_idx   = lsd[5];

    uint8_t gct[256][3];
    memset(gct, 0, sizeof(gct));
    if (has_gct) {
        for (int i = 0; i < gct_size; i++)
            if (fread(gct[i], 1, 3, f) != 3) goto fail;
    }

    /* Working buffers */
    int np    = cw * ch;
    canvas      = calloc(np, sizeof(uint16_t));   /* RGBA16, initially transparent */
    prev_canvas = calloc(np, sizeof(uint16_t));
    indices     = malloc(np);
    if (!canvas || !prev_canvas || !indices) goto fail;

    /* Per-frame GCE state */
    int gce_delay_ms = 100;
    int gce_trans    = -1;
    int gce_disposal = 0;

    /* Block loop */
    for (;;) {
        uint8_t tag;
        if (fread(&tag, 1, 1, f) != 1) break;
        if (tag == 0x3B) break;     /* Trailer */

        /* ---- Extension ---- */
        if (tag == 0x21) {
            uint8_t label;
            if (fread(&label, 1, 1, f) != 1) break;

            if (label == 0xF9) {    /* Graphics Control Extension */
                uint8_t buf[6];     /* block_size + 4 data bytes + terminator */
                if (fread(buf, 1, 6, f) != 6) break;
                gce_disposal = (buf[1] >> 2) & 7;
                int cs       = buf[2] | (buf[3] << 8);   /* centiseconds */
                gce_delay_ms = (cs > 0) ? cs * 10 : 100;
                gce_trans    = (buf[1] & 1) ? (int)(uint8_t)buf[4] : -1;
            } else {
                skip_sub_blocks(f);
            }
            continue;
        }

        if (tag != 0x2C) { skip_sub_blocks(f); continue; }

        /* ---- Image Descriptor ---- */
        uint8_t id[9];
        if (fread(id, 1, 9, f) != 9) break;

        int ix      = id[0] | (id[1] << 8);
        int iy      = id[2] | (id[3] << 8);
        int iw      = id[4] | (id[5] << 8);
        int ih      = id[6] | (id[7] << 8);
        int ipacked = id[8];
        int has_lct = (ipacked >> 7) & 1;
        int lct_size = 1 << ((ipacked & 7) + 1);

        /* Clamp image rect to canvas */
        if (ix + iw > cw) iw = cw - ix;
        if (iy + ih > ch) ih = ch - iy;
        if (iw <= 0 || ih <= 0) { skip_sub_blocks(f); goto next_frame; }

        /* Active colour table */
        uint8_t lct[256][3];
        const uint8_t (*ct)[3] = (const uint8_t (*)[3])gct;
        if (has_lct) {
            for (int i = 0; i < lct_size; i++)
                if (fread(lct[i], 1, 3, f) != 3) { skip_sub_blocks(f); goto next_frame; }
            ct = (const uint8_t (*)[3])lct;
        }

        /* LZW minimum code size */
        uint8_t min_lzw;
        if (fread(&min_lzw, 1, 1, f) != 1) goto next_frame;
        if (min_lzw < 2 || min_lzw > 11) { skip_sub_blocks(f); goto next_frame; }

        /* Read & decompress frame data */
        int     clen  = 0;
        uint8_t *cdata = read_sub_blocks(f, &clen);
        if (!cdata) goto next_frame;

        int decoded = lzw_decompress(cdata, clen, indices, iw * ih, (int)min_lzw);
        free(cdata);

        /* Composite frame onto canvas */
        memcpy(prev_canvas, canvas, np * sizeof(uint16_t));

        for (int row = 0; row < ih; row++) {
            for (int col = 0; col < iw; col++) {
                int src_idx = (decoded > 0) ? (int)indices[row * iw + col] : bg_idx;
                if (gce_trans >= 0 && src_idx == gce_trans) continue; /* transparent */
                uint8_t r = ct[src_idx][0];
                uint8_t g = ct[src_idx][1];
                uint8_t b = ct[src_idx][2];
                /* RGBA5551: RRRRRGGGGGGBBBBBBBBA (5-5-5-1) */
                canvas[(iy + row) * cw + (ix + col)] = (uint16_t)(
                    ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1
                );
            }
        }

        /* Store frame as RGBA16 surface */
        if (gif->frame_count < GIF_MAX_FRAMES) {
            surface_t *surf = malloc(sizeof(surface_t));
            if (surf) {
                *surf = surface_alloc(FMT_RGBA16, cw, ch);
                if (surf->buffer) {
                    for (int row = 0; row < ch; row++) {
                        memcpy((uint8_t *)surf->buffer + row * surf->stride,
                               canvas + row * cw,
                               cw * sizeof(uint16_t));
                    }
                    gif->frames[gif->frame_count].frame    = surf;
                    gif->frames[gif->frame_count].delay_ms = (uint16_t)gce_delay_ms;
                    gif->frame_count++;
                } else {
                    free(surf);
                }
            }
        }

        /* Apply disposal for next frame compositing */
        if (gce_disposal == 2) {
            /* Restore to background (transparent) */
            for (int row = iy; row < iy + ih && row < ch; row++)
                for (int col = ix; col < ix + iw && col < cw; col++)
                    canvas[row * cw + col] = 0;
        } else if (gce_disposal == 3) {
            /* Restore to previous frame */
            memcpy(canvas, prev_canvas, np * sizeof(uint16_t));
        }
        /* Disposal 0 and 1: leave canvas as-is */

next_frame:
        gce_trans    = -1;
        gce_delay_ms = 100;
        gce_disposal = 0;
        if (gif->frame_count >= GIF_MAX_FRAMES) break;
    }

    free(canvas);
    free(prev_canvas);
    free(indices);
    fclose(f);

    if (gif->frame_count == 0) {
        gif_free(gif);
        return NULL;
    }
    return gif;

fail:
    free(canvas);
    free(prev_canvas);
    free(indices);
    if (f) fclose(f);
    gif_free(gif);
    return NULL;
}

/* ----------------------------------------------------------------
 * gif_free
 * ---------------------------------------------------------------- */
void gif_free (gif_image_t *gif) {
    if (!gif) return;
    for (int i = 0; i < gif->frame_count; i++) {
        if (gif->frames[i].frame) {
            surface_free(gif->frames[i].frame);
            free(gif->frames[i].frame);
            gif->frames[i].frame = NULL;
        }
    }
    free(gif);
}
