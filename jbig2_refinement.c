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
 * Generic Refinement region handlers.
 **/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "os_types.h"

#include <stddef.h>
#include <string.h>             /* memcpy(), memset() */

#include <stdio.h>

#include "jbig2.h"
#include "jbig2_priv.h"
#include "jbig2_arith.h"
#include "jbig2_generic.h"
#include "jbig2_image.h"

#if 0
#define DBG(format, ...) fprintf(stderr, "%s: " format, __func__,  __VA_ARGS__)
#else
#define DBG(format, ...) /**/
#endif
#undef LEGACY
#undef NO_INLINE_MKCTX
#undef NO_INLINE_IMPLICIT_VALUE
#undef NO_INLINE_PIXEL_ACCESS
#undef NO_NOMINAL

#ifdef NO_INLINE_PIXEL_ACCESS
/* uses: x, y */
#define GETRELPIX0(IM,L,GBW,GBH,XOFF,YOFF,OFFSET) \
  jbig2_image_get_pixel(IM, x + (XOFF), y + (YOFF))
#define SETPIXNOCHECK(IM,L,X,Y,BIT) \
  jbig2_image_set_pixel(IM, X, Y, BIT)
#else
/* uses: x, y */
/* requires: L + OFFSET == data + (y + YOFF)*stride */
#define GETRELPIX0(IM,L,GBW,GBH,XOFF,YOFF,OFFSET) \
  ((x + XOFF < 0 || x + XOFF >= GBW || \
    y + YOFF < 0 || y + YOFF >= GBH) \
   ? 0 \
   : (/*assert((L) + (OFFSET) == (IM)->data + (IM)->stride*(y + (YOFF))),*/ \
      L[((x + (XOFF)) >> 3) + (OFFSET)] >> (7 - ((x + (XOFF)) & 7))) & 1 )
#define SETPIXNOCHECK(IM,L,X,Y,BIT) \
  (/*assert((X) >= 0 && (X) < (IM)->width && \
          (Y) >= 0 && (Y) < (IM)->height && \
          L == ((byte *)(IM)->data) + (IM)->stride*(Y) && \
          ((BIT)&~1) == 0),*/ \
   L[(X)>>3] = (L[(X)>>3] & ~(1<<(7-((X) & 7))))|(BIT<<(7-((X) & 7))))
#endif

/** Simplified GETRELPIX0 wrapper for image.
 * uses: GRW [= image->width], GRH [= image->height], x, y (origin point in image), image, imline [= image->data + y*image->stride] */
#define GETRELPIX0_image(XOFF_,YOFF_,OFFSET_) \
  GETRELPIX0(image, imline, GRW, GRH, XOFF_, YOFF_, OFFSET_)
/** Simplified GETRELPIX0 wrapper for ref.
 * uses: GXW [= ref->width], GXH [= ref->height], x, y (origin point in image), dx [= params->DX], dy [= params->DY], ref, refline [= ref->data + (y - dy) * ref->stride] */
#define GETRELPIX0_ref(XOFF_,YOFF_,OFFSET_) \
  GETRELPIX0(ref, refline,  GXW, GXH, (XOFF_)-dx, (YOFF_)-dy, OFFSET_)

/** Simplified GETRELPIX0_image wrapper for constant XOFF and YOFF.
 * uses: same as GETRELPIX0_image, and imstride [= image->stride] */
#define GETRELPIX0_image_const(XOFF__, YOFF__) \
  GETRELPIX0_image((XOFF__),(YOFF__),(YOFF__)*imstride)
/** Simplified GETRELPIX0_ref wrapper for constant XOFF and YOFF.
 * uses: same as GETRELPIX0_ref, and refstride [= ref->stride] */
#define GETRELPIX0_ref_const(XOFF__, YOFF__) \
  GETRELPIX0_ref((XOFF__),(YOFF__),(YOFF__)*refstride)

/* Declare and assign variables for GETRELPIX0_image */
#define GETRELPIX_image_vars \
  int x,y; \
  const int GRW = image->width; \
  const int GRH = image->height; \
  const int imstride = image->stride; \
  byte *imline = (byte *)image->data
/* Declare and assign variables for GETRELPIX0_ref */
#define GETRELPIX_ref_vars \
  const Jbig2Image * const ref = params->reference; \
  const int dx = params->DX; \
  const int dy = params->DY; \
  const int GXW = ref->width; \
  const int GXH = ref->height; \
  const int refstride = ref->stride; \
  const byte *refline = ((const byte *)ref->data) - dy*refstride

#if 0                           /* currently not used */
/* TODO: implement with assumption of nominal position and (DX & 7) == 0 */
static int
jbig2_decode_refinement_template0(Jbig2Ctx *ctx,
                                  Jbig2Segment *segment,
                                  const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "refinement region template 0 NYI");
}
#endif

static int
jbig2_decode_refinement_template0_nominal(Jbig2Ctx *ctx,
        Jbig2Segment *segment,
        const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    GETRELPIX_image_vars;
    GETRELPIX_ref_vars;

    /* nominal position: GRAT[XY][01] -1, -1, -1, -1 */
    DBG("dxy=(%+d,%+d)\n", dx, dy);
    for (y = 0; y < GRH; y++, imline += imstride, refline += refstride) {
        bool bit;
        uint32_t CONTEXT = 0;

        x = 0;
#ifndef LEGACY
        /* XXX MKCTX0N_PRE and MKCTX0N_KEEP_MASK are also used in jbig2_decode_refinement_TPGRON */
#define MKCTX0N_PRE(INIT) do { \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
    if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, -1) << 3; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +1) <<  4; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +1) <<  5; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, +1) <<  6; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +0) <<  7; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +0) <<  8; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, +0) <<  9; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, -1) << 10; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, -1) << 11; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, -1) << 12; \
} while(0)
#define MKCTX0N_KEEP_MASK \
      ((1<<2)|(1<<3)|(1<<5)|(1<<6)|(1<<8)|(1<<9)|(1<<11)|(1<<12))
        /* set bits 0, 2, 3, 5, 6, 8, 9, 11, 12 for first iteration */
        MKCTX0N_PRE(TRUE);
#endif
        for (; x < GRW; x++) {
#ifndef LEGACY
            /* set bits 1, 4, 7, 10 for all iterations */
            MKCTX0N_PRE(FALSE);
#else
            CONTEXT = 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y + 0) << 0;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 3;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 1) << 4;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 0) << 7;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 0) << 8;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 0) << 9;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy - 1) << 10;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy - 1) << 11;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy - 1) << 12;
#endif
            bit = jbig2_arith_decode(as, &GR_stats[CONTEXT]);
            if (bit < 0)
                return -1;
            SETPIXNOCHECK(image, imline, x, y, bit);
#ifndef LEGACY
            /* calculate bits 0, 2, 3, 5, 6, 8, 9, 11, 12 for next iteration */
            CONTEXT = ((CONTEXT << 1) & MKCTX0N_KEEP_MASK) | bit;
#endif
        }
    }
#ifdef JBIG2_DEBUG_DUMP
    {
        static count = 0;
        char name[32];

        snprintf(name, 32, "refin-%d.pbm", count);
        jbig2_image_write_pbm_file(ref, name);
        snprintf(name, 32, "refout-%d.pbm", count);
        jbig2_image_write_pbm_file(image, name);
        count++;
    }
#endif

    return 0;
}

static int
jbig2_decode_refinement_template0_unopt(Jbig2Ctx *ctx,
                                        Jbig2Segment *segment,
                                        const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    GETRELPIX_image_vars;
    GETRELPIX_ref_vars;
    const int xoffI = params->grat[0];
    const int yoffI = params->grat[1];
    const int offsetI = imstride * yoffI;
    const int xoffR = params->grat[2];
    const int yoffR = params->grat[3];
    const int offsetR = refstride * yoffR;

    /* TODO optimized special-case (DX & 7) == 0 */
    DBG("dxy=(%+d,%+d) off={ (%+d,%+d), (%+d,%+d) }\n", dx, dy, xoffI, yoffI, xoffR, yoffR);
    for (y = 0; y < GRH; y++, imline += imstride, refline += refstride) {
        bool bit;
        uint32_t CONTEXT = 0;

        x = 0;
#ifndef LEGACY
        /* XXX MKCTX0G_PRE and MKCTX0G_KEEP_MASK are also used in jbig2_decode_refinement_TPGRON */
#define MKCTX0G_PRE(INIT) do { \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
    if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
    if (!INIT) CONTEXT |= GETRELPIX0_image(xoffI, yoffI, offsetI) << 3; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +1) << 4; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +1) << 5; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, +1) << 6; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +0) << 7; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +0) << 8; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, +0) << 9; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, -1) << 10; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, -1) << 11; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref(xoffR, yoffR, offsetR) << 12; \
} while(0)
#define MKCTX0G_KEEP_MASK \
      ((1<<2)|(1<<5)|(1<<6)|(1<<8)|(1<<9)|(1<<11))
        /* set bits 0, 2, 5, 6, 8, 9, 11 for first iteration */
        MKCTX0G_PRE(TRUE);
#endif
        for (; x < GRW; x++) {
#ifndef LEGACY
            /* set bits 1, 3, 4, 7, 10, 12 for all iterations */
            MKCTX0G_PRE(FALSE);
#else
            CONTEXT = 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y + 0) << 0;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x + params->grat[0], y + params->grat[1]) << 3;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 1) << 4;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 1) << 6;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 0) << 7;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 0) << 8;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 0) << 9;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy - 1) << 10;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy - 1) << 11;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + params->grat[2], y - dy + params->grat[3]) << 12;
#endif
            bit = jbig2_arith_decode(as, &GR_stats[CONTEXT]);
            if (bit < 0)
                return -1;
            SETPIXNOCHECK(image, imline, x, y, bit);
#ifndef LEGACY
            /* calculate bits 0, 2, 5, 6, 8, 9, 11 for next iteration */
            CONTEXT = ((CONTEXT << 1) & MKCTX0G_KEEP_MASK) | bit;
#endif
        }
    }
#ifdef JBIG2_DEBUG_DUMP
    {
        static count = 0;
        char name[32];

        snprintf(name, 32, "refin-%d.pbm", count);
        jbig2_image_write_pbm_file(ref, name);
        snprintf(name, 32, "refout-%d.pbm", count);
        jbig2_image_write_pbm_file(image, name);
        count++;
    }
#endif

    return 0;
}

static int
jbig2_decode_refinement_template1_unopt(Jbig2Ctx *ctx,
                                        Jbig2Segment *segment,
                                        const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    GETRELPIX_image_vars;
    GETRELPIX_ref_vars;

    DBG("dxy=(%+d,%+d)\n", dx, dy);
    for (y = 0; y < GRH; y++, imline += imstride, refline += refstride) {
        uint32_t CONTEXT = 0;

        x = 0;
#ifndef LEGACY
        /* XXX MKCTX1_PRE and MKCTX1_KEEP_MASK are also used in jbig2_decode_refinement_TPGRON */
#define MKCTX1_PRE(INIT) do { \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
    if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
    if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, -1) << 3; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +1) << 4; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +1) << 5; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+1, +0) << 6; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(+0, +0) << 7; \
    if ( INIT) CONTEXT |= GETRELPIX0_ref_const(-1, +0) << 8; \
    if (!INIT) CONTEXT |= GETRELPIX0_ref_const(+0, -1) << 9; \
} while(0)
#define MKCTX1_KEEP_MASK \
      ((1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8))
        /* set bits 0, 2, 3, 5, 7, 8 for first iteration */
        MKCTX1_PRE(TRUE);
#endif
        for (; x < GRW; x++) {
            bool bit;

#ifndef LEGACY
            /* set bits 1, 3, 6, 9 for all iterations */
            MKCTX1_PRE(FALSE);
#else
            CONTEXT = 0;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y + 0) << 0;
            CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 1;
            CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 2;
            CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 3;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 1) << 4;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 1) << 5;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 0) << 6;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 0) << 7;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 0) << 8;
            CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy - 1) << 9;
#endif
            bit = jbig2_arith_decode(as, &GR_stats[CONTEXT]);
            if (bit < 0)
                return -1;
            SETPIXNOCHECK(image, imline, x, y, bit);
#ifndef LEGACY
            /* calculate bits 0, 2, 3, 5, 7, 8 for next iteration */
            CONTEXT = ((CONTEXT << 1) & MKCTX1_KEEP_MASK) | bit;
#endif
        }
    }

#ifdef JBIG2_DEBUG_DUMP
    {
        static count = 0;
        char name[32];

        snprintf(name, 32, "refin-%d.pbm", count);
        jbig2_image_write_pbm_file(ref, name);
        snprintf(name, 32, "refout-%d.pbm", count);
        jbig2_image_write_pbm_file(image, name);
        count++;
    }
#endif

    return 0;
}

#if 0                           /* currently not used */
/* TODO: fix/rewrite with assumption of (DX % 8) == 0 */
static int
jbig2_decode_refinement_template1(Jbig2Ctx *ctx,
                                  Jbig2Segment *segment,
                                  const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    const int GRW = image->width;
    const int GRH = image->height;
    const int stride = image->stride;
    const int refstride = params->reference->stride;
    const int dy = params->DY;
    byte *grreg_line = (byte *) image->data;
    byte *grref_line = (byte *) params->reference->data;
    int x, y;

    for (y = 0; y < GRH; y++) {
        const int padded_width = (GRW + 7) & -8;
        uint32_t CONTEXT;
        uint32_t refline_m1;    /* previous line of the reference bitmap */
        uint32_t refline_0;     /* current line of the reference bitmap */
        uint32_t refline_1;     /* next line of the reference bitmap */
        uint32_t line_m1;       /* previous line of the decoded bitmap */

        /* TODO ensure padding bits in last byte cleared (ref!!!) */
        line_m1 = (y >= 1) ? grreg_line[-stride] : 0;
        refline_m1 = ((y - dy) >= 1) ? grref_line[(-1 - dy) * stride] << 2 : 0;
        refline_0 = (((y - dy) > 0) && ((y - dy) < GRH)) ? grref_line[(0 - dy) * stride] << 4 : 0;
        refline_1 = (y < GRH - 1) ? grref_line[(+1 - dy) * stride] << 7 : 0;
        CONTEXT = ((line_m1 >> 5) & 0x00e) | ((refline_1 >> 5) & 0x030) | ((refline_0 >> 5) & 0x1c0) | ((refline_m1 >> 5) & 0x200);

        for (x = 0; x < padded_width; x += 8) {
            byte result = 0;
            int x_minor;
            const int minor_width = GRW - x > 8 ? 8 : GRW - x;

            if (y >= 1) {
                line_m1 = (line_m1 << 8) | (x + 8 < GRW ? grreg_line[-stride + (x >> 3) + 1] : 0);
                refline_m1 = (refline_m1 << 8) | (x + 8 < GRW ? grref_line[-refstride + (x >> 3) + 1] << 2 : 0);
            }

            refline_0 = (refline_0 << 8) | (x + 8 < GRW ? grref_line[(x >> 3) + 1] << 4 : 0);

            if (y < GRH - 1)
                refline_1 = (refline_1 << 8) | (x + 8 < GRW ? grref_line[+refstride + (x >> 3) + 1] << 7 : 0);
            else
                refline_1 = 0;

            /* this is the speed critical inner-loop */
            for (x_minor = 0; x_minor < minor_width; x_minor++) {
                bool bit;

                bit = jbig2_arith_decode(as, &GR_stats[CONTEXT]);
                if (bit < 0)
                    return -1;
                result |= bit << (7 - x_minor);
                CONTEXT = ((CONTEXT & 0x0d6) << 1) | bit |
                          ((line_m1 >> (9 - x_minor)) & 0x002) |
                          ((refline_1 >> (9 - x_minor)) & 0x010) | ((refline_0 >> (9 - x_minor)) & 0x040) | ((refline_m1 >> (9 - x_minor)) & 0x200);
            }

            grreg_line[x >> 3] = result;

        }

        grreg_line += stride;
        grref_line += refstride;

    }

    return 0;

}
#endif

typedef uint32_t(*ContextBuilder)(const Jbig2RefinementRegionParams *, Jbig2Image *, int, int);

#if defined(NO_INLINE_IMPLICIT_VALUE)
static int
implicit_value(const Jbig2RefinementRegionParams *params, Jbig2Image *image, int x, int y)
{
    Jbig2Image *ref = params->reference;
    int i = x - params->DX;
    int j = y - params->DY;
    int m = jbig2_image_get_pixel(ref, i, j);

    return ((jbig2_image_get_pixel(ref, i - 1, j - 1) == m) &&
            (jbig2_image_get_pixel(ref, i, j - 1) == m) &&
            (jbig2_image_get_pixel(ref, i + 1, j - 1) == m) &&
            (jbig2_image_get_pixel(ref, i - 1, j) == m) &&
            (jbig2_image_get_pixel(ref, i + 1, j) == m) &&
            (jbig2_image_get_pixel(ref, i - 1, j + 1) == m) && (jbig2_image_get_pixel(ref, i, j + 1) == m) && (jbig2_image_get_pixel(ref, i + 1, j + 1) == m)
           )? m : -1;
}
#endif

#if defined(NO_INLINE_MKCTX) || defined(NO_INLINE_IMPLICIT_VALUE)
static uint32_t
mkctx0(const Jbig2RefinementRegionParams *params, Jbig2Image *image, int x, int y)
{
    const int dx = params->DX;
    const int dy = params->DY;
    Jbig2Image *ref = params->reference;
    uint32_t CONTEXT;

    CONTEXT = jbig2_image_get_pixel(image, x - 1, y + 0);
    CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 1;
    CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 2;
    CONTEXT |= jbig2_image_get_pixel(image, x + params->grat[0], y + params->grat[1]) << 3;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 1) << 4;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 1) << 5;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 1) << 6;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 0) << 7;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 0) << 8;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 0) << 9;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy - 1) << 10;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy - 1) << 11;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + params->grat[2], y - dy + params->grat[3]) << 12;
    return CONTEXT;
}

static uint32_t
mkctx1(const Jbig2RefinementRegionParams *params, Jbig2Image *image, int x, int y)
{
    const int dx = params->DX;
    const int dy = params->DY;
    Jbig2Image *ref = params->reference;
    uint32_t CONTEXT;

    CONTEXT = jbig2_image_get_pixel(image, x - 1, y + 0);
    CONTEXT |= jbig2_image_get_pixel(image, x + 1, y - 1) << 1;
    CONTEXT |= jbig2_image_get_pixel(image, x + 0, y - 1) << 2;
    CONTEXT |= jbig2_image_get_pixel(image, x - 1, y - 1) << 3;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 1) << 4;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 1) << 5;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 1, y - dy + 0) << 6;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy + 0) << 7;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx - 1, y - dy + 0) << 8;
    CONTEXT |= jbig2_image_get_pixel(ref, x - dx + 0, y - dy - 1) << 9;
    return CONTEXT;
}
#endif

static int
jbig2_decode_refinement_TPGRON(const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    GETRELPIX_image_vars;
    GETRELPIX_ref_vars;
    int bit, LTP = 0;
    const bool GRTEMPLATE = params->GRTEMPLATE;
    uint32_t start_context = (GRTEMPLATE ? 0x40 : 0x100);

#if defined(NO_INLINE_MKCTX) || defined(NO_INLINE_IMPLICIT_VALUE)
    ContextBuilder mkctx = (GRTEMPLATE ? mkctx1 : mkctx0);
#endif
    const int xoffI = params->grat[0];
    const int yoffI = params->grat[1];
    const int offsetI = imstride * yoffI;
    const int xoffR = params->grat[2];
    const int yoffR = params->grat[3];
    const int offsetR = refstride * yoffR;
    const bool nominal = GRTEMPLATE ? 0 : xoffI == -1 && yoffI == -1 && xoffR == -1 && yoffR == -1;

    if (GRTEMPLATE)
        DBG("GRTEMPLATE: %d\n", GRTEMPLATE);
    else
        DBG("GRTEMPLATE: %d off: (%+d,%+d) (%+d,%+d)\n", GRTEMPLATE, xoffI, yoffI, xoffR, xoffR);

    for (y = 0; y < GRH; y++, imline += imstride, refline += refstride) {
        bit = jbig2_arith_decode(as, &GR_stats[start_context]);
        if (bit < 0)
            return -1;
        LTP = LTP ^ bit;
        if (!LTP) {
#if defined(NO_INLINE_MKCTX)
            for (x = 0; x < GRW; x++) {
                bit = jbig2_arith_decode(as, &GR_stats[mkctx(params, image, x, y)]);
                if (bit < 0)
                    return -1;
                SETPIXNOCHECK(image, imline, x, y, bit);
            }
#else
            uint32_t CONTEXT = 0;

            x = 0;
#define INNER_LOOP(MKCTX_PRE, MKCTX_KEEP_MASK) do {                     \
            MKCTX_PRE(TRUE);                                            \
            for (; x < GRW; x++) {                                      \
                MKCTX_PRE(FALSE);                                       \
                bit = jbig2_arith_decode(as, &GR_stats[CONTEXT]);       \
                if (bit < 0)                                            \
                    return -1;                                          \
                SETPIXNOCHECK(image, imline, x, y, bit);                \
                CONTEXT = ((CONTEXT << 1) & MKCTX_KEEP_MASK) | bit;     \
            }                                                           \
} while(0)
            if (GRTEMPLATE)
                INNER_LOOP(MKCTX1_PRE, MKCTX1_KEEP_MASK);
#ifndef NO_NOMINAL
            else if (nominal)
                INNER_LOOP(MKCTX0N_PRE, MKCTX0N_KEEP_MASK);
#endif
            else
                INNER_LOOP(MKCTX0G_PRE, MKCTX0G_KEEP_MASK);
#undef INNER_LOOP
#endif
        } else {
#if !defined(NO_INLINE_IMPLICIT_VALUE)
#define MKIVCTX_PRE(INIT) do { \
        if (!INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+1, +1) << 0; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+0, +1) << 1; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(-1, +1) << 2; \
        if (!INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+1, +0) << 3; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+0, +0) << 4; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(-1, +0) << 5; \
        if (!INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+1, -1) << 6; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(+0, -1) << 7; \
        if ( INIT) IV_CONTEXT |= GETRELPIX0_ref_const(-1, -1) << 8; \
} while(0)
#define MKIVCTX_KEEP_MASK \
      ((1<<1)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<8))

#ifndef NO_REUSE_IMPLICIT_VALUE
            /* GRTEMPLATE == 1, reuse IV_CONTEXT */
#define MKCTX1i_PRE(INIT) do { \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
      if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, -1) << 3; \
      if (!INIT) CONTEXT |= (IV_CONTEXT & (1<<0)) << (4-0)/*ref(+1, +1)<<4*/; \
      if ( INIT) CONTEXT |= (IV_CONTEXT & (1<<1)) << (5-1)/*ref(+0, +1)<<5*/; \
      if (!INIT) CONTEXT |= (IV_CONTEXT & (1<<3)) << (6-3)/*ref(+1, +0)<<6*/; \
      if ( INIT) CONTEXT |= (IV_CONTEXT & (1<<4)) << (7-4)/*ref(+0, +0)<<7*/; \
      if ( INIT) CONTEXT |= (IV_CONTEXT & (1<<5)) << (8-5)/*ref(-1, +0)<<8*/; \
      if (!INIT) CONTEXT |= (IV_CONTEXT & (1<<7)) << (9-7)/*ref(+0, -1)<<9*/; \
} while(0)
#define MKCTX1i_KEEP_MASK MKCTX1_KEEP_MASK

            /* GRTEMPLATE == 0, generic, reuse IV_CONTEXT */
#define MKCTX0Gi_PRE(INIT) do { \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
      if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
      if (!INIT) CONTEXT |= GETRELPIX0_image(xoffI, yoffI, offsetI) << 3; \
      if (!INIT) CONTEXT |= (IV_CONTEXT << 4) & ~(1<<12); \
      if (!INIT) CONTEXT |= GETRELPIX0_ref(xoffR, yoffR, offsetR) << 12; \
} while(0)
#define MKCTX0Gi_KEEP_MASK ((1<<2))

            /* GRTEMPLATE == 0, nominal, reuse IV_CONTEXT */
#define MKCTX0Ni_PRE(INIT) do { \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, +0) << 0; \
      if (!INIT) CONTEXT |= GETRELPIX0_image_const(+1, -1) << 1; \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(+0, -1) << 2; \
      if ( INIT) CONTEXT |= GETRELPIX0_image_const(-1, -1) << 3; \
      if (!INIT) CONTEXT |= (IV_CONTEXT << 4); \
} while(0)
#define MKCTX0Ni_KEEP_MASK ((1<<2)|(1<<3))
#else
#define MKCTX0Gi_PRE       MKCTX0G_PRE
#define MKCTX0Gi_KEEP_MASK MKCTX0G_KEEP_MASK
#define MKCTX0Ni_PRE       MKCTX0N_PRE
#define MKCTX0Ni_KEEP_MASK MKCTX0N_KEEP_MASK
#define MKCTX1i_PRE        MKCTX1_PRE
#define MKCTX1i_KEEP_MASK  MKCTX1_KEEP_MASK
#endif
#endif
#if defined(NO_INLINE_IMPLICIT_VALUE)
            for (x = 0; x < GRW; x++) {
                int iv;

                iv = implicit_value(params, image, x, y);
                if (iv < 0) {
                    bit = jbig2_arith_decode(as, &GR_stats[mkctx(params, image, x, y)]);
                    if (bit < 0)
                        return -1;
                    SETPIXNOCHECK(image, imline, x, y, bit);
                } else
                    SETPIXNOCHECK(image, imline, x, y, iv);
            }
#elif defined(NO_INLINE_MKCTX)
            uint32_t IV_CONTEXT = 0;

            x = 0;
            /* set part of IV_CONTEXT for first iteration */
            MKIVCTX_PRE(TRUE);
            for (; x < GRW; x++) {
                int m = (IV_CONTEXT >> 4) & 1 /*ref_const(+0, +0) */ ;
                int mmask = ((1 << 9) - 1) & ~(m - 1) /* m ? ((1<<9)-1) : 0 */ ;

                /* set part of IV_CONTEXT for all iterations */
                MKIVCTX_PRE(FALSE);
                if ((mmask ^ IV_CONTEXT)) {
                    m = jbig2_arith_decode(as, &GR_stats[mkctx(params, image, x, y)]);
                    if (m < 0)
                        return -1;
                }
                SETPIXNOCHECK(image, imline, x, y, m);
            }
#else
            uint32_t IV_CONTEXT = 0;

            x = 0;
            /* set part of IV_CONTEXT for first iteration */
            MKIVCTX_PRE(TRUE);
#define INNER_LOOP(MKCTX_PRE, MKCTX_KEEP_MASK) do {                     \
            uint32_t CONTEXT = (1<<20); /* INVALID, requires reinit */  \
            for (; x < GRW; x++) {                                      \
                int m = (IV_CONTEXT >> 4) & 1/*ref_const(+0, +0)*/;     \
                int mmask = ((1<<9)-1)&~(m-1) /* m ? ((1<<9)-1) : 0 */; \
                /* set part of IV_CONTEXT for all iterations */         \
                MKIVCTX_PRE(FALSE);                                     \
                if ((mmask ^ IV_CONTEXT)) {                             \
                    if (CONTEXT == (1<<20)) {                           \
                        CONTEXT = 0;                                    \
                        MKCTX_PRE(TRUE);                                \
                    }                                                   \
                    MKCTX_PRE(FALSE);                                   \
                    m = jbig2_arith_decode(as, &GR_stats[CONTEXT]);     \
                    if (m < 0) return -1;                               \
                    CONTEXT = ((CONTEXT << 1) & MKCTX_KEEP_MASK) | m;   \
                } else {                                                \
                    CONTEXT = (1<<20); /* INVALID, requires re-init */  \
                }                                                       \
                SETPIXNOCHECK(image, imline, x, y, m);                  \
                /* calculate part of IV_CONTEXT for next iteration */   \
                IV_CONTEXT = (IV_CONTEXT << 1) & MKIVCTX_KEEP_MASK;     \
            }                                                           \
} while(0)
            if (GRTEMPLATE)
                INNER_LOOP(MKCTX1i_PRE, MKCTX1_KEEP_MASK);
#ifndef NO_NOMINAL
            else if (nominal)
                INNER_LOOP(MKCTX0Ni_PRE, MKCTX0Ni_KEEP_MASK);
#endif
            else
                INNER_LOOP(MKCTX0Gi_PRE, MKCTX0Gi_KEEP_MASK);
#undef INNER_LOOP
#endif
#undef MKIVCTX_PRE
#undef MKIVCTX_KEEP_MASK
        }
    }

    return 0;
}

/**
 * jbig2_decode_refinement_region: Decode a generic refinement region.
 * @ctx: The context for allocation and error reporting.
 * @segment: A segment reference for error reporting.
 * @params: Decoding parameter set.
 * @as: Arithmetic decoder state.
 * @image: Where to store the decoded image.
 * @GR_stats: Arithmetic stats.
 *
 * Decodes a generic refinement region, according to section 6.3.
 * an already allocated Jbig2Image object in @image for the result.
 *
 * Because this API is based on an arithmetic decoding state, it is
 * not suitable for MMR decoding.
 *
 * Return code: 0 on success.
 **/
int
jbig2_decode_refinement_region(Jbig2Ctx *ctx,
                               Jbig2Segment *segment,
                               const Jbig2RefinementRegionParams *params, Jbig2ArithState *as, Jbig2Image *image, Jbig2ArithCx *GR_stats)
{
    {
        jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number,
                    "decoding generic refinement region with offset %d,%x, GRTEMPLATE=%d, TPGRON=%d",
                    params->DX, params->DY, params->GRTEMPLATE, params->TPGRON);
    }

    if (params->TPGRON)
        return jbig2_decode_refinement_TPGRON(params, as, image, GR_stats);

    if (params->GRTEMPLATE)
        return jbig2_decode_refinement_template1_unopt(ctx, segment, params, as, image, GR_stats);
#ifndef NO_NOMINAL
    else if (params->grat[0] == -1 && params->grat[1] == -1 && params->grat[2] == -1 && params->grat[3] == -1)
        return jbig2_decode_refinement_template0_nominal(ctx, segment, params, as, image, GR_stats);
#endif
    else
        return jbig2_decode_refinement_template0_unopt(ctx, segment, params, as, image, GR_stats);
}

/**
 * Find the first referred-to intermediate region segment
 * with a non-NULL result for use as a reference image
 */
static Jbig2Segment *
jbig2_region_find_referred(Jbig2Ctx *ctx, Jbig2Segment *segment)
{
    const int nsegments = segment->referred_to_segment_count;
    Jbig2Segment *rsegment;
    int index;

    for (index = 0; index < nsegments; index++) {
        rsegment = jbig2_find_segment(ctx, segment->referred_to_segments[index]);
        if (rsegment == NULL) {
            jbig2_error(ctx, JBIG2_SEVERITY_WARNING, segment->number, "could not find referred to segment %d", segment->referred_to_segments[index]);
            continue;
        }
        switch (rsegment->flags & 63) {
        case 4:                /* intermediate text region */
        case 20:               /* intermediate halftone region */
        case 36:               /* intermediate generic region */
        case 40:               /* intermediate generic refinement region */
            if (rsegment->result)
                return rsegment;
            break;
        default:               /* keep looking */
            break;
        }
    }
    /* no appropriate reference was found. */
    return NULL;
}

/**
 * Handler for generic refinement region segments
 */
int
jbig2_refinement_region(Jbig2Ctx *ctx, Jbig2Segment *segment, const byte *segment_data)
{
    Jbig2RefinementRegionParams params;
    Jbig2RegionSegmentInfo rsi;
    int offset = 0;
    byte seg_flags;
    int code = 0;

    /* 7.4.7 */
    if (segment->data_length < 18)
        return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "Segment too short");

    jbig2_get_region_segment_info(&rsi, segment_data);
    jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number, "generic region: %d x %d @ (%d, %d), flags = %02x", rsi.width, rsi.height, rsi.x, rsi.y, rsi.flags);

    /* 7.4.7.2 */
    seg_flags = segment_data[17];
    params.GRTEMPLATE = seg_flags & 0x01;
    params.TPGRON = seg_flags & 0x02 ? 1 : 0;
    jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number,
                "segment flags = %02x %s%s", seg_flags, params.GRTEMPLATE ? " GRTEMPLATE" : "", params.TPGRON ? " TPGRON" : "");
    if (seg_flags & 0xFC)
        jbig2_error(ctx, JBIG2_SEVERITY_WARNING, segment->number, "reserved segment flag bits are non-zero");
    offset += 18;

    /* 7.4.7.3 */
    if (!params.GRTEMPLATE) {
        if (segment->data_length < 22)
            return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "Segment too short");
        params.grat[0] = segment_data[offset + 0];
        params.grat[1] = segment_data[offset + 1];
        params.grat[2] = segment_data[offset + 2];
        params.grat[3] = segment_data[offset + 3];
        jbig2_error(ctx, JBIG2_SEVERITY_INFO, segment->number,
                    "grat1: (%d, %d) grat2: (%d, %d)", params.grat[0], params.grat[1], params.grat[2], params.grat[3]);
        offset += 4;
    }

    /* 7.4.7.4 - set up the reference image */
    if (segment->referred_to_segment_count) {
        Jbig2Segment *ref;

        ref = jbig2_region_find_referred(ctx, segment);
        if (ref == NULL)
            return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "could not find reference bitmap!");
        /* the reference bitmap is the result of a previous
           intermediate region segment; the reference selection
           rules say to use the first one available, and not to
           reuse any intermediate result, so we simply clone it
           and free the original to keep track of this. */
        params.reference = jbig2_image_clone(ctx, (Jbig2Image *) ref->result);
        jbig2_image_release(ctx, (Jbig2Image *) ref->result);
        ref->result = NULL;
        jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number, "found reference bitmap in segment %d", ref->number);
    } else {
        /* the reference is just (a subset of) the page buffer */
        params.reference = jbig2_image_clone(ctx, ctx->pages[ctx->current_page].image);
        if (params.reference == NULL)
            return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "could not clone reference bitmap!");
        /* TODO: subset the image if appropriate */
    }

    /* 7.4.7.5 */
    params.DX = 0;
    params.DY = 0;
    {
        Jbig2WordStream *ws = NULL;
        Jbig2ArithState *as = NULL;
        Jbig2ArithCx *GR_stats = NULL;
        int stats_size;
        Jbig2Image *image = NULL;

        image = jbig2_image_new(ctx, rsi.width, rsi.height);
        if (image == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, segment->number, "unable to allocate refinement image");
            goto cleanup;
        }
        jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number, "allocated %d x %d image buffer for region decode results", rsi.width, rsi.height);

        stats_size = params.GRTEMPLATE ? 1 << 10 : 1 << 13;
        GR_stats = jbig2_new(ctx, Jbig2ArithCx, stats_size);
        if (GR_stats == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "failed to allocate GR-stats in jbig2_refinement_region");
            goto cleanup;
        }
        memset(GR_stats, 0, stats_size);

        ws = jbig2_word_stream_buf_new(ctx, segment_data + offset, segment->data_length - offset);
        if (ws == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "failed to allocate ws in jbig2_refinement_region");
            goto cleanup;
        }

        as = jbig2_arith_new(ctx, ws);
        if (as == NULL) {
            code = jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "failed to allocate as in jbig2_refinement_region");
            goto cleanup;
        }

        code = jbig2_decode_refinement_region(ctx, segment, &params, as, image, GR_stats);

        if ((segment->flags & 63) == 40) {
            /* intermediate region. save the result for later */
            segment->result = jbig2_image_clone(ctx, image);
        } else {
            /* immediate region. composite onto the page */
            jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, segment->number,
                        "composing %dx%d decoded refinement region onto page at (%d, %d)", rsi.width, rsi.height, rsi.x, rsi.y);
            jbig2_page_add_result(ctx, &ctx->pages[ctx->current_page], image, rsi.x, rsi.y, rsi.op);
        }

cleanup:
        jbig2_image_release(ctx, image);
        jbig2_image_release(ctx, params.reference);
        jbig2_free(ctx->allocator, as);
        jbig2_word_stream_buf_free(ctx, ws);
        jbig2_free(ctx->allocator, GR_stats);
    }

    return code;
}
