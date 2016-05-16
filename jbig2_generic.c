/* Copyright (C) 2001-2012 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  7 Mt. Lassen Drive - Suite A-134, San Rafael,
   CA  94903, U.S.A., +1(415)492-9861, for further information.
*/

/*
    jbig2dec
*/

/**
 * Generic region handlers.
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "os_types.h"

#include <stddef.h>
#include <string.h>             /* memcpy(), memset() */

#ifdef OUTPUT_PBM
#include <stdio.h>
#endif

#include "jbig2.h"
#include "jbig2_priv.h"
#include "jbig2_image.h"
#include "jbig2_arith.h"
#include "jbig2_generic.h"
#include "jbig2_mmr.h"

/* return the appropriate context size for the given template */
int
jbig2_generic_stats_size(Jbig2Ctx *ctx, int template)
{
    int stats_size = template == 0 ? 1 << 16 : template == 1 ? 1 << 1 << 13 : 1 << 10;

                                        return stats_size;
}

static void
copy_prev_row(Jbig2Image *image, int row)
{
    if (!row) {
        /* no previous row */
        memset(image->data, 0, image->stride);
    } else {
        /* duplicate data from the previous row */
        uint8_t *src = image->data + (row - 1) * image->stride;

        memcpy(src + image->stride, src, image->stride);
    }
}

#undef LEGACY
#undef NO_INLINE_PIXEL_ACCESS
#if 0
#define DBG(format, ...) fprintf(stderr, "%s: " format, __func__,  __VA_ARGS__)
#else
#define DBG(format, ...) /**/
#endif
#ifdef NO_INLINE_PIXEL_ACCESS
#define GETRELPIX0(XOFF,YOFF,OFFSET) \
    jbig2_image_get_pixel(image, x + x_minor + XOFF, y + YOFF)
#define SETPIXNOCHECK(IM,L,X,Y,BIT) \
    jbig2_image_set_pixel(IM, X, Y, BIT)
#else
/* Current position is assumed to be (x + x_minor, y) */
/* uses: GBW [= image->width], GBH [= IM->height], gbreg_line [= IM->data + y*IM->stride], uses: x, x_minor, y */
/* OFFSET is byte offset to line: OFFSET = YOFF*image->stride */
#define GETRELPIX0(XOFF,YOFF,OFFSET) \
    ((y + (YOFF) < 0 || y + (YOFF) >= GBH || \
      x + x_minor + (XOFF) < 0 || x + x_minor + (XOFF) >= GBW) \
     ? 0 \
     : (gbreg_line[((x + x_minor + (XOFF)) >> 3) + OFFSET] >> (7 - ((x + x_minor + XOFF) & 7))) & 1 )
#define SETPIXNOCHECK(IM,L,X,Y,BIT) \
    /* assert((X) >= 0 && (X) < (IM)->width && \
              (Y) >= 0 && (Y) < (IM)->height && \
              L == ((byte *)(IM)->data) + (IM)->stride*(Y) && \
              ((BIT)&~1) == 0),*/ \
    (L[(X)>>3] = (L[(X)>>3] & ~(1<<(7-((X) & 7))))|(BIT<<(7-((X) & 7))))
#endif
static int
jbig2_decode_generic_template0_nominal(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    int x, y;
    byte *gbreg_line = (byte *) image->data;
    bool LTP = 0;

    /* optimized, only handles the nominal gbat location: 2, -1 */

#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif
    DBG("TPGDON=%d %dx%d\n", TPGDON, GBW, GBH);

    if (GBW <= 0)
        return 0;

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        uint32_t line_m2;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x9B25]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        /*
         * WARNING:
         * line_m1/m2 loading requires that all extra bits of last byte
         * of incomplete line are cleared;
         * 1) First line:
         *   a)  LTP completely clears line;
         *   b) !LTP clears extra bits in last byte when writing `result`
         * 2) Next lines:
         *   a)  LTP copies previous line (cleared);
         *   b) !LTP same as (1a);
         */
        /* assert(y < 1 || 0 == (gbreg_line[-1*rowstride + (GBW >> 3)] & ((1 << ((-GBW)&7))-1))); */
        /* assert(y < 2 || 0 == (gbreg_line[-2*rowstride + (GBW >> 3)] & ((1 << ((-GBW)&7))-1))); */
        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        line_m2 = (y >= 2) ? gbreg_line[-(rowstride << 1)] << 6 : 0;
                                        CONTEXT = (line_m1 & 0x7f0) | (line_m2 & 0xf800);

                                        /* 6.2.5.7 3d */
        for (x = 0; x < GBW; x += 8) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? gbreg_line[-rowstride + (x >> 3) + 1] : 0);

            if (y >= 2)
                line_m2 = (line_m2 << 8) | (x + 8 < GBW ? gbreg_line[-(rowstride << 1) + (x >> 3) + 1] << 6 : 0);

                       /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0x7bf7) << 1) | bit | ((line_m1 >> (7 - x_minor)) & 0x10) | ((line_m2 >> (7 - x_minor)) & 0x800);
            }
            gbreg_line[x >> 3] = result;
        }
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

static int
jbig2_decode_generic_template0_generic(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    int x, y;
    const int xoff0 = params->gbat[0];
    const int yoff0 = params->gbat[1];
    const int xoff1 = params->gbat[2];
    const int yoff1 = params->gbat[3];
    const int xoff2 = params->gbat[4];
    const int yoff2 = params->gbat[5];
    const int xoff3 = params->gbat[6];
    const int yoff3 = params->gbat[7];
    const int rowstride = image->stride;
    const int offset0 = yoff0 * rowstride;
    const int offset1 = yoff1 * rowstride;
    const int offset2 = yoff2 * rowstride;
    const int offset3 = yoff3 * rowstride;
    byte *gbreg_line = (byte *) image->data;
    bool LTP = 0;
    const byte wmask = 0xff << ((-GBW) & 7);

    DBG("TPGDON=%d %dx%d, off = { (%+d,%+d) (%+d,%+d) (%+d,%+d) }\n", TPGDON, GBW, GBH, xoff0, yoff0, xoff1, yoff1, xoff2, yoff2);
    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t line_m1;
        uint32_t line_m2;
        uint32_t CONTEXT;
        byte *px;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x9B25]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }
#ifdef LEGACY
        /* this version is generic and easy to understand, but very slow */
        for (x = 0; x < GBW; x++) {
            bool bit;

            CONTEXT = 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y) << 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x - 3, y) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x - 4, y) << 3;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[0], y + params->gbat[1]) << 4;
            CONTEXT |= jbig2_image_get_pixel(image, x + 2, y - 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 7;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 8;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y - 1) << 9;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[2], y + params->gbat[3]) << 10;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[4], y + params->gbat[5]) << 11;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 2) << 12;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 2) << 13;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 2) << 14;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[6], y + params->gbat[7]) << 15;
            bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
            if (bit < 0)
                        return -1;
            jbig2_image_set_pixel(image, x, y, bit);
        }
#else
        /* optimized version of above code */
        line_m1 = (y >= 1) ? gbreg_line[-(rowstride * 1)] : 0;
        line_m2 = (y >= 2) ? gbreg_line[-(rowstride * 2)] << 6 : 0;
        CONTEXT = (             /* put pixels ( 0,-1), (+1,-1), (+2,-1) to bits 7, 6, 5 */
            ((line_m1) & 0x0e0) |
            /* put pixels ( 0,-2), (+1,-2) to bits 13, 12 */
            ((line_m2) & 0x3000));

        /* clear extra bits in last pixel */
        gbreg_line[GBW >> 3] &= wmask;

        for (x = 0, px = gbreg_line; x < GBW; x += 8, px++) {
            byte result = 0;
            byte mask = 0x7f;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? px[-(rowstride * 1) + 1] : 0);
                       if (y >= 2)
                           line_m2 = (line_m2 << 8) | (x + 8 < GBW ? px[-(rowstride * 2) + 1] << 6 : 0);

                                                   /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++, mask >>= 1) {
                bool bit;

                /* bits 0..3,  5..9,  13..14 already set */
                CONTEXT |= GETRELPIX0(xoff0, yoff0, offset0) << 4;
                CONTEXT |= GETRELPIX0(xoff1, yoff1, offset1) << 10;
                CONTEXT |= GETRELPIX0(xoff2, yoff2, offset2) << 11;
                CONTEXT |= GETRELPIX0(xoff3, yoff3, offset3) << 15;
                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                /*
                 * flush result to image, GETRELPIX0(xoffX, yoffX) may access it;
                 * note: this won't clear extra bits of last byte, so we had to
                 * do this explicitly
                 */
                *px = (*px & mask) | result;
                /* prepare next CONTEXT:
                 * shift by one pixel and clear bits 0, 4, 5, 10, 11, 12, 15;
                 */
                CONTEXT = ((CONTEXT << 1) & 0x63ce) |
                           /* put current pixel to bit 0 */
                           bit |
                           /* put next iteration pixel (+2,-1) to bit 5 */
                           ((line_m1 >> (7 - x_minor)) & 0x020) |
                /* put next iteration pixel (+1,-2) to bit 12 */
                ((line_m2 >> (7 - x_minor)) & 0x1000);
            }
        }
#endif
    }
    return 0;
}

static int
jbig2_decode_generic_template1_nominal(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    int x, y;
    byte *gbreg_line = (byte *) image->data;
    bool LTP = 0;

    /* only handles the nominal gbat location: 3, -1 */

    DBG("TPGDON=%d %dx%d\n", TPGDON, GBW, GBH);
#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif

    if (GBW <= 0)
                return 0;

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        uint32_t line_m2;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x0795]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        line_m2 = (y >= 2) ? gbreg_line[-(rowstride << 1)] << 5 : 0;
                                        CONTEXT = ((line_m1 >> 1) & 0x1f8) | ((line_m2 >> 1) & 0x1e00);

        /* 6.2.5.7 3d */
        for (x = 0; x < GBW; x += 8) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? gbreg_line[-rowstride + (x >> 3) + 1] : 0);

            if (y >= 2)
                line_m2 = (line_m2 << 8) | (x + 8 < GBW ? gbreg_line[-(rowstride << 1) + (x >> 3) + 1] << 5 : 0);

                       /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0xefb) << 1) | bit | ((line_m1 >> (8 - x_minor)) & 0x8) | ((line_m2 >> (8 - x_minor)) & 0x200);
            }
            gbreg_line[x >> 3] = result;
        }
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

static int
jbig2_decode_generic_template1_generic(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    const int xoff = params->gbat[0];
    const int yoff = params->gbat[1];
    const int offset = yoff * rowstride;
    byte *gbreg_line = (byte *) image->data;
    int x, y;
    int LTP = 0;

    DBG("TPGDON=%d %dx%d, off = { %+d,%+d }\n", TPGDON, GBW, GBH, xoff, yoff);
    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        uint32_t line_m2;
        const int x_minor = 0;  /* dummy for GETRELPIX0 */

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x0795]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        x = 0;
        /* TODO: consider further optimization, see template0 */
#ifndef LEGACY
        /* Some bits of next CONTEXT can be calculated by shifting
         * current CONTEXT and adding only missing bits.
         */
#define MKCTX_PRE(INIT) do { \
        if( INIT) CONTEXT |= GETRELPIX0(-1, +0, +0*rowstride) << 0; \
        if( INIT) CONTEXT |= GETRELPIX0(-2, +0, +0*rowstride) << 1; \
        if( INIT) CONTEXT |= GETRELPIX0(-3, +0, +0*rowstride) << 2; \
        if(!INIT) CONTEXT |= GETRELPIX0(xoff, yoff, offset) << 3; \
        if(!INIT) CONTEXT |= GETRELPIX0(+2, -1, -1*rowstride) << 4; \
        if( INIT) CONTEXT |= GETRELPIX0(+1, -1, -1*rowstride) << 5; \
        if( INIT) CONTEXT |= GETRELPIX0(+0, -1, -1*rowstride) << 6; \
        if( INIT) CONTEXT |= GETRELPIX0(-1, -1, -1*rowstride) << 7; \
        if( INIT) CONTEXT |= GETRELPIX0(-2, -1, -1*rowstride) << 8; \
        if(!INIT) CONTEXT |= GETRELPIX0(+2, -2, -2*rowstride) << 9; \
        if( INIT) CONTEXT |= GETRELPIX0(+1, -2, -2*rowstride) << 10; \
        if( INIT) CONTEXT |= GETRELPIX0(+0, -2, -2*rowstride) << 11; \
        if( INIT) CONTEXT |= GETRELPIX0(-1, -2, -2*rowstride) << 12; \
} while(0)
#define MKCTX_KEEP_MASK \
            ((1<<1)|(1<<2)|(1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<10)|(1<<11)|(1<<12))
        CONTEXT = 0;
        /* set bits 0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12 for first iteration: */
        MKCTX_PRE(TRUE);
#endif
        for (; x < GBW; x++) {
            bool bit;

#ifdef LEGACY
            CONTEXT = jbig2_image_get_pixel(image, x - 1, y);
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x - 3, y) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[0], y + params->gbat[1]) << 3;
            CONTEXT |= jbig2_image_get_pixel(image, x + 2, y - 1) << 4;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(image, x, y - 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 7;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y - 1) << 8;
            CONTEXT |= jbig2_image_get_pixel(image, x + 2, y - 2) << 9;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 2) << 10;
            CONTEXT |= jbig2_image_get_pixel(image, x, y - 2) << 11;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 2) << 12;
#else
            /* set missing bits 2, 3, 7 for all iterations */
            MKCTX_PRE(FALSE);
#endif
            bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
            if (bit < 0)
                        return -1;
            SETPIXNOCHECK(image, gbreg_line, x, y, bit);
#ifndef LEGACY
            /* calculate next bits 0, 1, 2, 4, 5, 6, 7, 8, 10, 11, 12;
             * set next bit 0 to current pixel
             */
            CONTEXT = ((CONTEXT << 1) & MKCTX_KEEP_MASK) | bit;
#undef MKCTX_KEEP_MASK
#undef MKCTX_PRE
#endif
        }
    }

    return 0;
}

static int
jbig2_decode_generic_template2_nominal(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    int x, y;
    byte *gbreg_line = (byte *) image->data;
    bool LTP = 0;

    /* only handles the nominal gbat location: 2, -1 */

#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif

    if (GBW <= 0)
                return 0;

    DBG("TPGDON=%d %dx%d\n", TPGDON, GBW, GBH);

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        uint32_t line_m2;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0xE5]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        line_m2 = (y >= 2) ? gbreg_line[-(rowstride << 1)] << 4 : 0;
                                        CONTEXT = ((line_m1 >> 3) & 0x7c) | ((line_m2 >> 3) & 0x380);

        /* 6.2.5.7 3d */
        for (x = 0; x < GBW; x += 8) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? gbreg_line[-rowstride + (x >> 3) + 1] : 0);

            if (y >= 2)
                line_m2 = (line_m2 << 8) | (x + 8 < GBW ? gbreg_line[-(rowstride << 1) + (x >> 3) + 1] << 4 : 0);

                       /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0x1bd) << 1) | bit | ((line_m1 >> (10 - x_minor)) & 0x4) | ((line_m2 >> (10 - x_minor)) & 0x80);
            }
            gbreg_line[x >> 3] = result;
        }
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

static int
jbig2_decode_generic_template2a(Jbig2Ctx *ctx,
                                Jbig2Segment *segment,
                                const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    int x, y;
    byte *gbreg_line = (byte *) image->data;
    bool LTP = 0;

    /* This is a special case for GBATX1 = 3, GBATY1 = -1 */

    /* TODO may be genearilized for GBATX1 = 1..7 */
    DBG("TPGDON=%d %dx%d\n", TPGDON, GBW, GBH);

#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif

    if (GBW <= 0)
                return 0;

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        uint32_t line_m2;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0xE5]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        line_m2 = (y >= 2) ? gbreg_line[-(rowstride << 1)] << 4 : 0;
                                        CONTEXT = ((line_m1 >> 3) & 0x78) | ((line_m1 >> 2) & 0x4) | ((line_m2 >> 3) & 0x380);

        /* 6.2.5.7 3d */
        for (x = 0; x < GBW; x += 8) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? gbreg_line[-rowstride + (x >> 3) + 1] : 0);

            if (y >= 2)
                line_m2 = (line_m2 << 8) | (x + 8 < GBW ? gbreg_line[-(rowstride << 1) + (x >> 3) + 1] << 4 : 0);

                       /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0x1b9) << 1) | bit |
                           ((line_m1 >> (10 - x_minor)) & 0x8) | ((line_m1 >> (9 - x_minor)) & 0x4) | ((line_m2 >> (10 - x_minor)) & 0x80);
            }
            gbreg_line[x >> 3] = result;
        }
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

static int
jbig2_decode_generic_template2_generic(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    int x, y;
    const int xoff = params->gbat[0];
    const int yoff = params->gbat[1];
    const int offset = yoff * rowstride;
    int LTP = 0;
    byte *gbreg_line = (byte *) image->data;

    DBG("TPGDON=%d %dx%d, off = { %+d,%+d }\n", TPGDON, GBW, GBH, xoff, yoff);

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        const int x_minor = 0;  /* dummy for GETRELPIX0 */

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0xE5]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }
        x = 0;
#ifndef LEGACY
        /* TODO: consider further optimization, see template0 */
#define MKCTX_PRE(INIT) do { \
      if( INIT) CONTEXT  = GETRELPIX0(-1, +0, +0*rowstride) << 0; \
      if( INIT) CONTEXT |= GETRELPIX0(-2, +0, +0*rowstride) << 1; \
      if(!INIT) CONTEXT |= GETRELPIX0(xoff, yoff, offset) << 2; \
      if(!INIT) CONTEXT |= GETRELPIX0(+1, -1, -1*rowstride) << 3; \
      if( INIT) CONTEXT |= GETRELPIX0( 0, -1, -1*rowstride) << 4; \
      if( INIT) CONTEXT |= GETRELPIX0(-1, -1, -1*rowstride) << 5; \
      if( INIT) CONTEXT |= GETRELPIX0(-2, -1, -1*rowstride) << 6; \
      if(!INIT) CONTEXT |= GETRELPIX0(+1, -2, -2*rowstride) << 7; \
      if( INIT) CONTEXT |= GETRELPIX0( 0, -2, -2*rowstride) << 8; \
      if( INIT) CONTEXT |= GETRELPIX0(-1, -2, -2*rowstride) << 9; \
} while(0)
#define MKCTX_KEEP_MASK \
    ((1<<1)|(1<<4)|(1<<5)|(1<<6)|(1<<8)|(1<<9))
        CONTEXT = 0;
        /* set bits 0, 1, 4, 5, 6, 8, 9 for first iteration: */
        MKCTX_PRE(TRUE);
#endif
        for (; x < GBW; x++) {
            bool bit;

#ifdef LEGACY
            CONTEXT = jbig2_image_get_pixel(image, x - 1, y);
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[0], y + params->gbat[1]) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 3;
            CONTEXT |= jbig2_image_get_pixel(image, x, y - 1) << 4;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y - 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 2) << 7;
            CONTEXT |= jbig2_image_get_pixel(image, x, y - 2) << 8;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 2) << 9;
#else
            /* set missing bits 3, 4, 9 for all iterations */
            MKCTX_PRE(FALSE);
#endif
            bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
            if (bit < 0)
                        return -1;
            SETPIXNOCHECK(image, gbreg_line, x, y, bit);
#ifndef LEGACY
            /* calculate next bits 1, 4, 5, 6, 8, 9;
             * set next bit 0 to current pixel
             */
            CONTEXT = ((CONTEXT << 1) & MKCTX_KEEP_MASK) | bit;
#undef MKCTX_KEEP_MASK
#undef MKCTX_PRE
#endif
        }
    }

    return 0;
}

static int
jbig2_decode_generic_template3_nominal(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int rowstride = image->stride;
    byte *gbreg_line = (byte *) image->data;
    int x, y;
    bool LTP = 0;

    /* this routine only handles the nominal AT location */

#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif

    if (GBW <= 0)
                return 0;

    DBG("TPGDON=%d %dx%d\n", TPGDON, GBW, GBH);

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x0195]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }

        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        CONTEXT = (line_m1 >> 1) & 0x3f0;

        /* 6.2.5.7 3d */
        for (x = 0; x < GBW; x += 8) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            /* note: requires extra bits of last byte *cleared* */
            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? gbreg_line[-rowstride + (x >> 3) + 1] : 0);

            /* This is the speed-critical inner loop. */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                if (bit < 0)
                            return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0x1f7) << 1) | bit | ((line_m1 >> (10 - x_minor)) & 0x010);
            }
            gbreg_line[x >> 3] = result;
        }
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

static int
jbig2_decode_generic_template3_generic(Jbig2Ctx *ctx,
                                       Jbig2Segment *segment,
                                       const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const bool TPGDON = params->TPGDON;
    const int GBW = image->width;
    const int GBH = image->height;
    const int xoff = params->gbat[0];
    const int yoff = params->gbat[1];
    const int rowstride = image->stride;
    byte *gbreg_line = (byte *) image->data;
    const int offset = yoff * rowstride;
    const byte wmask = 0xff << ((-GBW) & 7);
    int x, y;
    bool LTP = 0;

    if (GBW <= 0)
                return 0;

#ifdef OUTPUT_PBM
    printf("P4\n%d %d\n", GBW, GBH);
#endif

    DBG("TPGDON=%d %dx%d, off = { %+d,%+d }\n", TPGDON, GBW, GBH, xoff, yoff);

    for (y = 0; y < GBH; y++, gbreg_line += rowstride) {
        uint32_t CONTEXT;
        uint32_t line_m1;
        byte *px;

        if (TPGDON) {
            bool bit = jbig2_arith_decode(as, &GB_stats[0x0195]);

            if (bit < 0)
                        return -1;
            LTP ^= bit;
            if (LTP) {
                copy_prev_row(image, y);
                continue;
            }
        }
#ifdef LEGACY
        /* this version is generic and easy to understand, but very slow */
        for (x = 0; x < GBW; x++) {
            CONTEXT = 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y) << 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x - 3, y) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x - 4, y) << 3;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->gbat[0], y + params->gbat[1]) << 4;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 7;
            CONTEXT |= jbig2_image_get_pixel(image, x - 2, y - 1) << 8;
            CONTEXT |= jbig2_image_get_pixel(image, x - 3, y - 1) << 9;
            bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
            if (bit < 0)
                        return -1;
            jbig2_image_set_pixel(image, x, y, bit);
        }
#else
        line_m1 = (y >= 1) ? gbreg_line[-rowstride] : 0;
        CONTEXT = (line_m1 >> 1) & 0x3e0;

        /* clear extra bits in the last pixel */
        gbreg_line[GBW >> 3] &= wmask;

        /* 6.2.5.7 3d */
        for (x = 0, px = gbreg_line; x < GBW; x += 8, px++) {
            byte result = 0;
            int x_minor;
            int minor_width = GBW - x > 8 ? 8 : GBW - x;

            if (y >= 1)
                line_m1 = (line_m1 << 8) | (x + 8 < GBW ? px[-rowstride + 1] : 0);

                       /* This is the speed-critical inner loop. */
            if (yoff | (((~xoff) | -xoff) & ~7) /*yoff != 0 || xoff >= 0 || xoff < -7 */) {
                /* GETRELPIX0(xoff, yoff) never access updated part of result */
                for (x_minor = 0; x_minor < minor_width; x_minor++) {
                    bool bit;

                    CONTEXT |= GETRELPIX0(xoff, yoff, offset) << 4;
                    bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                    if (bit < 0)
                                return -1;
                    result |= bit << (7 - x_minor);
                    CONTEXT = ((CONTEXT & 0x1e7) << 1) | bit |
                               /* move pixel +1 (bit 6 + 8 = 14) to bit 5 of CONTEXT */
                               ((line_m1 >> (8 - x_minor)) & 0x020);
                }
                *px = result;
            } else {
                byte mask = 0x7f;

                for (x_minor = 0; x_minor < minor_width; x_minor++, mask >>= 1) {
                    bool bit;

                    CONTEXT |= GETRELPIX0(xoff, yoff, offset) << 4;
                    bit = jbig2_arith_decode(as, &GB_stats[CONTEXT]);
                    if (bit < 0)
                                return -1;
                    result |= bit << (7 - x_minor);
                    /* flush result to image, (xoff, yoff) might access it */
                    *px = (*px & mask) | result;
                    CONTEXT = ((CONTEXT & 0x1e7) << 1) | bit |
                               /* move pixel +1 (bit 6 + 8 = 14) to bit 5 of CONTEXT */
                               ((line_m1 >> (8 - x_minor)) & 0x020);
                }
            }
        }
#endif
#ifdef OUTPUT_PBM
        fwrite(gbreg_line, 1, rowstride, stdout);
#endif
    }

    return 0;
}

/**
 * jbig2_decode_generic_region: Decode a generic region.
 * @ctx: The context for allocation and error reporting.
 * @segment: A segment reference for error reporting.
 * @params: Decoding parameter set.
 * @as: Arithmetic decoder state.
 * @image: Where to store the decoded data.
 * @GB_stats: Arithmetic stats.
 *
 * Decodes a generic region, according to section 6.2. The caller should
 * pass an already allocated Jbig2Image object for @image
 *
 * Because this API is based on an arithmetic decoding state, it is
 * not suitable for MMR decoding.
 *
 * Return code: 0 on success.
 **/
int
jbig2_decode_generic_region(Jbig2Ctx *ctx,
                            Jbig2Segment *segment, const Jbig2GenericRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GB_stats)
{
    const int8_t *gbat = params->gbat;

    if (image->stride * image->height > (1 << 24) && segment->data_length < image->stride * image->height / 256) {
        return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number,
                           "region is far larger than data provided (%d << %d), aborting to prevent DOS", segment->data_length, image->stride * image->height);
    }

    if (!params->MMR)
        switch (params->GBTEMPLATE) {
        case 0:
#ifndef NO_NOMINAL
            if (gbat[0] == +3 && gbat[1] == -1 &&       /* nominal gbat position */
                    gbat[2] == -3 && gbat[3] == -1 && gbat[4] == +2 && gbat[5] == -2 && gbat[6] == -2 && gbat[7] == -2)
                return jbig2_decode_generic_template0_nominal(ctx, segment, params, as, image, GB_stats);
            else
#endif
                return jbig2_decode_generic_template0_generic(ctx, segment, params, as, image, GB_stats);
        case 1:
#ifndef NO_NOMINAL
            if (gbat[0] == +3 && gbat[1] == -1) /* nominal gbat position */
                return jbig2_decode_generic_template1_nominal(ctx, segment, params, as, image, GB_stats);
            else
#endif
                return jbig2_decode_generic_template1_generic(ctx, segment, params, as, image, GB_stats);
        case 2:
#ifndef NO_NOMINAL
            if (gbat[0] == +2 && gbat[1] == -1) /* nominal gbat position */
                return jbig2_decode_generic_template2_nominal(ctx, segment, params, as, image, GB_stats);
            else if (gbat[0] == +3 && gbat[1] == -1)    /* another special case */
                return jbig2_decode_generic_template2a(ctx, segment, params, as, image, GB_stats);
            else
#endif
                return jbig2_decode_generic_template2_generic(ctx, segment, params, as, image, GB_stats);
        case 3:
#if 0                           /* TODO find sample, verify and re-enable */
            if (gbat[0] == 2 && gbat[1] == -1)
                return jbig2_decode_generic_template3_nominal(ctx, segment, params, as, image, GB_stats);
            else
#endif
                return jbig2_decode_generic_template3_generic(ctx, segment, params, as, image, GB_stats);
        }

    {
        int i;

        for (i = 0; i < 8; i++)
                    jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number, "gbat[%d] = %d", i, params->gbat[i]);
    }
    jbig2_error(ctx, JBIG2_SEVERITY_WARNING, segment->number, "decode_generic_region: MMR=%d, GBTEMPLATE=%d NYI", params->MMR, params->GBTEMPLATE);
    return -1;
}

/**
 * Handler for immediate generic region segments
 */
int
jbig2_immediate_generic_region(Jbig2Ctx *ctx, Jbig2Segment *segment, const byte *segment_data)
{
    Jbig2RegionSegmentInfo rsi;
    byte seg_flags;
    int8_t gbat[8];
    int offset;
    int gbat_bytes = 0;
    Jbig2GenericRegionParams params;
    int code = 0;
    Jbig2Image *image = NULL;
    Jbig2WordStream *ws = NULL;
    Jbig2ArithState *as = NULL;
    Jbig2ArithCx *GB_stats = NULL;

    /* 7.4.6 */
    if (segment->data_length < 18)
                return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "Segment too short");

    jbig2_get_region_segment_info(&rsi, segment_data);
    jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number, "generic region: %d x %d @ (%d, %d), flags = %02x", rsi.width, rsi.height, rsi.x, rsi.y, rsi.flags);

    /* 7.4.6.2 */
    seg_flags = segment_data[17];
    jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number, "segment flags = %02x", seg_flags);
    if ((seg_flags & 1) && (seg_flags & 6))
        jbig2_error(ctx, JBIG2_SEVERITY_WARNING, segment->number, "MMR is 1, but GBTEMPLATE is not 0");

    /* 7.4.6.3 */
    if (!(seg_flags & 1)) {
        gbat_bytes = (seg_flags & 6) ? 2 : 8;
        if (18 + gbat_bytes > segment->data_length)
            return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "Segment too short");
        memcpy(gbat, segment_data + 18, gbat_bytes);
        jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number, "gbat: %d, %d", gbat[0], gbat[1]);
    }

    offset = 18 + gbat_bytes;

    /* Table 34 */
    params.MMR = seg_flags & 1;
    params.GBTEMPLATE = (seg_flags & 6) >> 1;
    params.TPGDON = (seg_flags & 8) >> 3;
    params.USESKIP = 0;
    memcpy(params.gbat, gbat, gbat_bytes);

    image = jbig2_image_new(ctx, rsi.width, rsi.height);
    if (image == NULL)
        return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "unable to allocate generic image");
    jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number, "allocated %d x %d image buffer for region decode results", rsi.width, rsi.height);

    if (params.MMR) {
        code = jbig2_decode_generic_mmr(ctx, segment, &params, segment_data + offset, segment->data_length - offset, image);
    } else {
        int stats_size = jbig2_generic_stats_size(ctx, params.GBTEMPLATE);

        GB_stats = jbig2_new(ctx, Jbig2ArithCx, stats_size);
        if (GB_stats == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "unable to allocate GB_stats in jbig2_immediate_generic_region");
            goto cleanup;
        }
        memset(GB_stats, 0, stats_size);

        ws = jbig2_word_stream_buf_new(ctx, segment_data + offset, segment->data_length - offset);
        if (ws == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "unable to allocate ws in jbig2_immediate_generic_region");
            goto cleanup;
        }
        as = jbig2_arith_new(ctx, ws);
        if (as == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "unable to allocate as in jbig2_immediate_generic_region");
            goto cleanup;
        }
        code = jbig2_decode_generic_region(ctx, segment, &params, as, image, GB_stats);
    }

    if (code >= 0)
        jbig2_page_add_result(ctx, &ctx->pages[ctx->current_page], image, rsi.x, rsi.y, rsi.op);
    else
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "error while decoding immediate_generic_region");

cleanup:
    jbig2_free(ctx->allocator, as);
    jbig2_word_stream_buf_free(ctx, ws);
    jbig2_free(ctx->allocator, GB_stats);
    jbig2_image_release(ctx, image);

    return code;
}
