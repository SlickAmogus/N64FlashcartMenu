/**
 * @file apng_decoder.c
 * @brief Minimal APNG / PNG decoder.
 * @ingroup menu
 *
 * Supports: colour types 2 (RGB), 6 (RGBA), 3 (indexed/palette).
 * Bit depth 8 only.  PNG filter types 0-4.  APNG blend_op 0/1,
 * dispose_op 0/1/2.  Plain PNGs decoded as a single frame.
 * Output: RGBA5551 surfaces compatible with rdpq_tex_blit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdragon.h>
#include <miniz.h>

#include "apng_decoder.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static uint32_t u32be (const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint16_t u16be (const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);
}

#define CHUNK(a,b,c,d) \
    (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(d))

static bool fread_exact (FILE *f, void *buf, size_t n) {
    return fread(buf, 1, n, f) == n;
}

/* ------------------------------------------------------------------ */
/* Growable byte buffer                                                  */
/* ------------------------------------------------------------------ */
typedef struct { uint8_t *data; size_t len, cap; } bytebuf_t;

static bool bb_append (bytebuf_t *b, const uint8_t *src, size_t n) {
    if (b->len + n > b->cap) {
        size_t nc = b->cap ? b->cap : 4096;
        while (nc < b->len + n) nc *= 2;
        uint8_t *nb = realloc(b->data, nc);
        if (!nb) return false;
        b->data = nb; b->cap = nc;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return true;
}
static void bb_free (bytebuf_t *b) { free(b->data); b->data=NULL; b->len=b->cap=0; }

/* ------------------------------------------------------------------ */
/* PNG row filter reconstruction                                         */
/* ------------------------------------------------------------------ */

static int paeth (int a, int b, int c) {
    int p=a+b-c, pa=abs(p-a), pb=abs(p-b), pc=abs(p-c);
    return (pa<=pb && pa<=pc) ? a : (pb<=pc ? b : c);
}

static void recon_row (uint8_t *row, const uint8_t *up, int w, int bpp, uint8_t ft) {
    switch (ft) {
        case 1:
            for (int x=bpp; x<w*bpp; x++) row[x] += row[x-bpp];
            break;
        case 2:
            for (int x=0; x<w*bpp; x++) row[x] += up[x];
            break;
        case 3:
            for (int x=0; x<w*bpp; x++) {
                uint8_t a = (x>=bpp) ? row[x-bpp] : 0;
                row[x] += (uint8_t)(((int)a + (int)up[x]) >> 1);
            }
            break;
        case 4:
            for (int x=0; x<w*bpp; x++) {
                uint8_t a=(x>=bpp)?row[x-bpp]:0, b=up[x], c=(x>=bpp)?up[x-bpp]:0;
                row[x] += (uint8_t)paeth(a,b,c);
            }
            break;
        default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Inflate + reconstruct a frame's zlib data into RGBA8                 */
/* ------------------------------------------------------------------ */
static uint8_t *inflate_to_rgba8 (const uint8_t *zdata, size_t zlen,
                                   int w, int h, int color_type,
                                   const uint8_t pal[256][3],
                                   const uint8_t trns_pal[256],
                                   int trns_r, int trns_g, int trns_b,
                                   bool has_trns) {
    int bpp = (color_type == 6) ? 4 : (color_type == 2) ? 3 : 1;

    mz_ulong raw_len = (mz_ulong)(h * (1 + w * bpp));
    uint8_t *raw = malloc(raw_len);
    if (!raw) return NULL;

    if (mz_uncompress(raw, &raw_len, zdata, (mz_ulong)zlen) != MZ_OK) {
        free(raw); return NULL;
    }

    uint8_t *rgba = malloc((size_t)(w * h * 4));
    if (!rgba) { free(raw); return NULL; }

    uint8_t *prev = calloc(1, (size_t)(w * bpp));
    if (!prev) { free(raw); free(rgba); return NULL; }

    size_t row_stride = (size_t)(1 + w * bpp);
    for (int y = 0; y < h; y++) {
        uint8_t *row = raw + y * row_stride + 1;
        recon_row(row, prev, w, bpp, raw[y * row_stride]);
        memcpy(prev, row, (size_t)(w * bpp));
        for (int x = 0; x < w; x++) {
            uint8_t *d = rgba + (y * w + x) * 4;
            if (color_type == 6) {
                d[0]=row[x*4]; d[1]=row[x*4+1]; d[2]=row[x*4+2]; d[3]=row[x*4+3];
            } else if (color_type == 2) {
                uint8_t r=row[x*3], g=row[x*3+1], b=row[x*3+2];
                d[0]=r; d[1]=g; d[2]=b;
                d[3]=(has_trns && r==(uint8_t)trns_r && g==(uint8_t)trns_g
                                && b==(uint8_t)trns_b) ? 0 : 255;
            } else {
                uint8_t idx=row[x];
                d[0]=pal[idx][0]; d[1]=pal[idx][1]; d[2]=pal[idx][2];
                d[3]=trns_pal[idx];
            }
        }
    }
    free(prev); free(raw);
    return rgba;
}

/* ------------------------------------------------------------------ */
/* Composite RGBA8 frame onto RGBA8 canvas                              */
/* ------------------------------------------------------------------ */
static void composite (uint8_t *canvas, int cw, int ch,
                       const uint8_t *frame, int fx, int fy, int fw, int fh,
                       uint8_t blend_op) {
    for (int y=0; y<fh; y++) {
        int cy=fy+y; if (cy<0||cy>=ch) continue;
        for (int x=0; x<fw; x++) {
            int cx=fx+x; if (cx<0||cx>=cw) continue;
            const uint8_t *s = frame  + (y*fw+x)*4;
            uint8_t       *d = canvas + (cy*cw+cx)*4;
            if (blend_op==0) {
                d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; d[3]=s[3];
            } else {
                int sa=s[3], da=d[3];
                int oa = sa + da*(255-sa)/255;
                if (oa==0) { d[0]=d[1]=d[2]=d[3]=0; }
                else {
                    d[0]=(uint8_t)((s[0]*sa + d[0]*da*(255-sa)/255)/oa);
                    d[1]=(uint8_t)((s[1]*sa + d[1]*da*(255-sa)/255)/oa);
                    d[2]=(uint8_t)((s[2]*sa + d[2]*da*(255-sa)/255)/oa);
                    d[3]=(uint8_t)oa;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Convert RGBA8 canvas to RGBA5551 surface                             */
/* ------------------------------------------------------------------ */
static surface_t *to_surface (const uint8_t *rgba, int w, int h) {
    surface_t *surf = malloc(sizeof(surface_t));
    if (!surf) return NULL;
    *surf = surface_alloc(FMT_RGBA16, w, h);
    if (!surf->buffer) { free(surf); return NULL; }
    for (int y=0; y<h; y++) {
        uint16_t *dst = (uint16_t*)((uint8_t*)surf->buffer + y*surf->stride);
        for (int x=0; x<w; x++) {
            const uint8_t *s = rgba + (y*w+x)*4;
            dst[x] = (s[3]<128) ? 0 :
                (uint16_t)(((s[0]>>3)<<11)|((s[1]>>3)<<6)|((s[2]>>3)<<1)|1);
        }
    }
    return surf;
}

/* ------------------------------------------------------------------ */
/* apng_load                                                             */
/* ------------------------------------------------------------------ */

/* State for the current pending frame (data buffered but not yet flushed) */
typedef struct {
    int      fw, fh, fx, fy;
    uint16_t delay_num, delay_den;
    uint8_t  dispose_op, blend_op;
    bool     set;       /* true when a fcTL has been received */
} fctl_t;

static void fctl_defaults (fctl_t *f, int w, int h) {
    f->fw=w; f->fh=h; f->fx=0; f->fy=0;
    f->delay_num=1; f->delay_den=10;
    f->dispose_op=0; f->blend_op=0;
    f->set=false;
}

/* Flush buffered zlib data as a new animation frame. */
static bool flush_frame (apng_image_t *apng,
                         const fctl_t *fc,
                         bytebuf_t *zbuf,
                         uint8_t *canvas, uint8_t *prev_canvas,
                         int cw, int ch, int color_type,
                         const uint8_t pal[256][3],
                         const uint8_t trns_pal[256],
                         int trns_r, int trns_g, int trns_b,
                         bool has_trns) {
    if (!zbuf->len || apng->frame_count >= APNG_MAX_FRAMES) {
        bb_free(zbuf);
        return false;
    }

    uint8_t *rgba = inflate_to_rgba8(zbuf->data, zbuf->len,
                                     fc->fw, fc->fh, color_type,
                                     pal, trns_pal,
                                     trns_r, trns_g, trns_b, has_trns);
    bb_free(zbuf);
    if (!rgba) return false;

    memcpy(prev_canvas, canvas, (size_t)(cw * ch * 4));
    composite(canvas, cw, ch, rgba, fc->fx, fc->fy, fc->fw, fc->fh, fc->blend_op);
    free(rgba);

    surface_t *surf = to_surface(canvas, cw, ch);
    if (surf) {
        uint16_t dn = fc->delay_num ? fc->delay_num : 1;
        uint16_t dd = fc->delay_den ? fc->delay_den : 100;
        int dms = (int)((uint32_t)dn * 1000u / dd);
        if (dms < 10) dms = 10;
        apng->frames[apng->frame_count].frame    = surf;
        apng->frames[apng->frame_count].delay_ms = (uint16_t)dms;
        apng->frame_count++;
    }

    /* Apply dispose_op for next frame */
    if (fc->dispose_op == 1) {
        for (int y=fc->fy; y<fc->fy+fc->fh && y<ch; y++)
            for (int x=fc->fx; x<fc->fx+fc->fw && x<cw; x++)
                memset(canvas + (y*cw+x)*4, 0, 4);
    } else if (fc->dispose_op == 2) {
        memcpy(canvas, prev_canvas, (size_t)(cw * ch * 4));
    }
    return true;
}

apng_image_t *apng_load (const char *path, int max_w, int max_h) {
    FILE         *f    = fopen(path, "rb");
    if (!f) return NULL;

    apng_image_t *apng = calloc(1, sizeof(apng_image_t));
    uint8_t      *canvas      = NULL;
    uint8_t      *prev_canvas = NULL;
    bytebuf_t     zbuf        = {0};

    if (!apng) goto fail;

    /* PNG signature */
    uint8_t sig[8];
    if (!fread_exact(f, sig, 8) ||
        memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) goto fail;

    /* Image properties */
    int width=0, height=0, color_type=0, bit_depth=0;
    bool ihdr_done = false;

    /* Palette */
    uint8_t pal[256][3];    memset(pal, 0, sizeof(pal));
    uint8_t trns_pal[256];  memset(trns_pal, 255, sizeof(trns_pal));
    int  trns_r=-1, trns_g=-1, trns_b=-1;
    bool has_trns = false;

    /* fcTL state */
    fctl_t cur_fc;
    memset(&cur_fc, 0, sizeof(cur_fc));

    uint8_t hdr[8];
    while (fread_exact(f, hdr, 8)) {
        uint32_t clen  = u32be(hdr);
        uint32_t ctype = u32be(hdr + 4);
        long     dpos  = ftell(f);

        if (ctype == CHUNK('I','H','D','R')) {
            uint8_t b[13];
            if (!fread_exact(f, b, 13)) goto fail;
            width      = (int)u32be(b+0);
            height     = (int)u32be(b+4);
            bit_depth  = b[8];
            color_type = b[9];
            if (width<=0 || height<=0 || width>max_w || height>max_h) goto fail;
            if (bit_depth != 8) goto fail;
            if (color_type!=2 && color_type!=3 && color_type!=6) goto fail;
            apng->width  = width;
            apng->height = height;
            canvas      = calloc(1, (size_t)(width * height * 4));
            prev_canvas = calloc(1, (size_t)(width * height * 4));
            if (!canvas || !prev_canvas) goto fail;
            fctl_defaults(&cur_fc, width, height);
            ihdr_done = true;

        } else if (ctype == CHUNK('P','L','T','E')) {
            int n = (int)(clen / 3); if (n>256) n=256;
            for (int i=0; i<n; i++)
                if (!fread_exact(f, pal[i], 3)) goto fail;

        } else if (ctype == CHUNK('t','R','N','S')) {
            if (color_type == 3) {
                uint32_t n = clen < 256 ? clen : 256;
                for (uint32_t i=0; i<n; i++) {
                    if (!fread_exact(f, &trns_pal[i], 1)) goto fail;
                }
            } else if (color_type == 2 && clen >= 6) {
                uint8_t tb[6];
                if (!fread_exact(f, tb, 6)) goto fail;
                trns_r = u16be(tb+0) & 0xFF;
                trns_g = u16be(tb+2) & 0xFF;
                trns_b = u16be(tb+4) & 0xFF;
                has_trns = true;
            }

        } else if (ctype == CHUNK('a','c','T','L')) {
            /* Just skip — we don't need anim frame count */
            if (fseek(f, (long)clen, SEEK_CUR) != 0) goto fail;

        } else if (ctype == CHUNK('f','c','T','L')) {
            if (!ihdr_done || clen < 26) goto skip;
            /* Flush any buffered frame data from the previous frame */
            if (zbuf.len > 0) {
                flush_frame(apng, &cur_fc, &zbuf,
                            canvas, prev_canvas, width, height, color_type,
                            (const uint8_t (*)[3])pal, trns_pal,
                            trns_r, trns_g, trns_b, has_trns);
            }
            uint8_t fc[26];
            if (!fread_exact(f, fc, 26)) goto fail;
            cur_fc.fw        = (int)u32be(fc+4);
            cur_fc.fh        = (int)u32be(fc+8);
            cur_fc.fx        = (int)u32be(fc+12);
            cur_fc.fy        = (int)u32be(fc+16);
            cur_fc.delay_num = u16be(fc+20);
            cur_fc.delay_den = u16be(fc+22);
            cur_fc.dispose_op= fc[24];
            cur_fc.blend_op  = fc[25];
            cur_fc.set       = true;

        } else if (ctype == CHUNK('I','D','A','T')) {
            if (!ihdr_done || !clen) goto skip;
            {
                uint8_t *tmp = malloc(clen);
                if (!tmp) goto fail;
                if (!fread_exact(f, tmp, clen)) { free(tmp); goto fail; }
                if (!bb_append(&zbuf, tmp, clen)) { free(tmp); goto fail; }
                free(tmp);
            }

        } else if (ctype == CHUNK('f','d','A','T')) {
            if (!ihdr_done || clen < 4) goto skip;
            if (fseek(f, 4, SEEK_CUR) != 0) goto fail; /* skip seq num */
            size_t dlen = (size_t)(clen - 4);
            if (dlen > 0) {
                uint8_t *tmp = malloc(dlen);
                if (!tmp) goto fail;
                if (!fread_exact(f, tmp, dlen)) { free(tmp); goto fail; }
                if (!bb_append(&zbuf, tmp, dlen)) { free(tmp); goto fail; }
                free(tmp);
            }

        } else if (ctype == CHUNK('I','E','N','D')) {
            break;

        } else {
skip:
            if (fseek(f, dpos + (long)clen, SEEK_SET) != 0) goto fail;
        }

        /* Skip CRC */
        if (fseek(f, dpos + (long)clen + 4, SEEK_SET) != 0) {
            /* Short file — stop gracefully */
            break;
        }
    }

    /* Flush the final (or only) frame */
    if (zbuf.len > 0) {
        flush_frame(apng, &cur_fc, &zbuf,
                    canvas, prev_canvas, width, height, color_type,
                    (const uint8_t (*)[3])pal, trns_pal,
                    trns_r, trns_g, trns_b, has_trns);
    }

    bb_free(&zbuf);
    free(canvas);
    free(prev_canvas);
    fclose(f);

    if (apng->frame_count == 0) { apng_free(apng); return NULL; }
    return apng;

fail:
    bb_free(&zbuf);
    free(canvas);
    free(prev_canvas);
    if (f) fclose(f);
    apng_free(apng);
    return NULL;

#undef CHUNK
}

/* ------------------------------------------------------------------ */
/* apng_free                                                             */
/* ------------------------------------------------------------------ */
void apng_free (apng_image_t *apng) {
    if (!apng) return;
    for (int i=0; i<apng->frame_count; i++) {
        if (apng->frames[i].frame) {
            surface_free(apng->frames[i].frame);
            free(apng->frames[i].frame);
        }
    }
    free(apng);
}
