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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "os_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>             /* memcpy() */
#include <limits.h>

#include "jbig2.h"
#include "jbig2_priv.h"
#include "jbig2_image.h"

/* allocate a Jbig2Image structure and its associated bitmap */
Jbig2Image *
jbig2_image_new(Jbig2Ctx *ctx, int width, int height)
{
    Jbig2Image *image;
    int stride;

    if (width <= 0 || height <= 0) {
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "negative dimentions (%dx%d)", width, height);
        return NULL;
    }
    image = jbig2_new(ctx, Jbig2Image, 1);
    if (image == NULL) {
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "could not allocate image structure in jbig2_image_new");
        return NULL;
    }

    stride = ((width - 1) >> 3) + 1;    /* generate a byte-aligned stride */
    /* check for integer multiplication overflow */
    if ((stride <= 0 || height >= (INT_MAX-1)/stride)) {
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "integer multiplication overflow from stride(%d)*height(%d)", stride, height);
        jbig2_free(ctx->allocator, image);
        return NULL;
    }
    /* Add 1 to accept runs that exceed image width and clamped to width+1 */
    image->data = jbig2_new(ctx, uint8_t, stride*height + 1);
    if (image->data == NULL) {
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "could not allocate image data buffer! [stride(%d)*height(%d) bytes]", stride, height);
        jbig2_free(ctx->allocator, image);
        return NULL;
    }

    if (width & 7) {
        int i, j;
        /* clear (partial) last byte */
        for(i = 0, j = (width >> 3); i < height; i ++, j += stride)
            image->data[j] = 0;
    }

    image->width = width;
    image->height = height;
    image->stride = stride;
    image->refcount = 1;

    return image;
}

/* clone an image pointer by bumping its reference count */
Jbig2Image *
jbig2_image_clone(Jbig2Ctx *ctx, Jbig2Image *image)
{
    if (image)
        image->refcount++;
    return image;
}

/* release an image pointer, freeing it it appropriate */
void
jbig2_image_release(Jbig2Ctx *ctx, Jbig2Image *image)
{
    if (image == NULL)
        return;
    image->refcount--;
    if (!image->refcount)
        jbig2_image_free(ctx, image);
}

/* free a Jbig2Image structure and its associated memory */
void
jbig2_image_free(Jbig2Ctx *ctx, Jbig2Image *image)
{
    if (image)
        jbig2_free(ctx->allocator, image->data);
    jbig2_free(ctx->allocator, image);
}

/* resize a Jbig2Image */
Jbig2Image *
jbig2_image_resize(Jbig2Ctx *ctx, Jbig2Image *image, int width, int height)
{
    if (width <= 0 || height <= 0) {
        jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "negative dimentions (%dx%d)", width, height);
        return NULL;
    }
    if (width == image->width) {
        /* check for integer multiplication overflow */
        if (/* image->stride >= 0 || */
            height >= (INT_MAX-1)/image->stride) {
            jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "integer multiplication overflow during resize stride(%d)*height(%d)", image->stride, height);
            return NULL;
        }
        /* use the same stride, just change the length */
        image->data = jbig2_renew(ctx, image->data, uint8_t, image->stride*height + 1);
        if (image->data == NULL) {
            jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "could not resize image buffer!");
            return NULL;
        }
        if (height > image->height) {
            memset(image->data + image->height * image->stride, 0, (height - image->height) * image->stride);
        }
        image->height = height;

    } else {
        /* we must allocate a new image buffer and copy */
        jbig2_error(ctx, JBIG2_SEVERITY_WARNING, -1, "jbig2_image_resize called with a different width (NYI)");
    }

    return NULL;
}

#ifdef NO_INLINE_PIXEL_ACCESS
#define image_get_pixel_unsafe(IM,L,X,Y) \
      jbig2_image_get_pixel(IM, (X), (Y))
#define image_set_pixel_unsafe(IM,L,X,Y,BIT) \
      jbig2_image_set_pixel(IM, X, Y, BIT)
#else
#define image_get_pixel_unsafe(IM,L,X,Y) \
    (/*assert((X) >= 0 && (X) < (IM)->width && \
       (Y) >= 0 && (Y) < (IM)->height && \
       L == ((byte *)(IM)->data) + (IM)->stride*(Y)),*/ \
     ((L[(X)>>3] >> (7-((X) & 7))) & 1))
#define image_set_pixel_unsafe(IM,L,X,Y,BIT) \
    (/*assert((X) >= 0 && (X) < (IM)->width && \
       (Y) >= 0 && (Y) < (IM)->height && \
       L == ((byte *)(IM)->data) + (IM)->stride*(Y) && \
       ((BIT)&~1) == 0),*/ \
     L[(X)>>3] = (L[(X)>>3] & ~(1<<(7-((X) & 7))))|((BIT)<<(7-((X) & 7))))
#endif
/* composite one jbig2_image onto another
   slow but general version */
static int
jbig2_image_compose_unopt(Jbig2Ctx *ctx, Jbig2Image *dst, Jbig2Image *src, int x, int y, Jbig2ComposeOp op)
{
    int i, j;
    int sw = src->width;
    int sh = src->height;
    int sx = 0;
    int sy = 0;
    const byte *srcline;
    byte *dstline;

    /* clip to the dst image boundaries */
    if (x < 0) {
        sx += -x;
        sw -= -x;
        x = 0;
    }
    if (y < 0) {
        sy += -y;
        sh -= -y;
        y = 0;
    }
    if (x + sw >= dst->width)
        sw = dst->width - x;
    if (y + sh >= dst->height)
        sh = dst->height - y;

    srcline = src->data + src->stride * sy;
    dstline = dst->data + dst->stride * y;
    /*
    assert(sw <= dst->width && sh <= dst->height);
    assert(sw <= src->width && sh <= src->height);
    assert(sh <= 0 || sw <= 0 || ( x >= 0 &&  y >= 0 &&  x <= dst->width-sw &&  y <= dst->height-sh));
    assert(sh <= 0 || sw <= 0 || (sx >= 0 && sy >= 0 && sx <= src->width-sw && sy <= src->height-sh));
    */
    switch (op) {
    case JBIG2_COMPOSE_OR:
        for (j = 0; j < sh; j++) {
            for (i = 0; i < sw; i++) {
                image_set_pixel_unsafe(dst, dstline, i + x, j + y,
                                       image_get_pixel_unsafe(src, srcline, i + sx, j + sy) | image_get_pixel_unsafe(dst, dstline, i + x, j + y));
            }
            srcline += src->stride;
            dstline += dst->stride;
        }
        break;
    case JBIG2_COMPOSE_AND:
        for (j = 0; j < sh; j++) {
            for (i = 0; i < sw; i++) {
                image_set_pixel_unsafe(dst, dstline, i + x, j + y,
                                       image_get_pixel_unsafe(src, srcline, i + sx, j + sy) & image_get_pixel_unsafe(dst, dstline, i + x, j + y));
            }
            srcline += src->stride;
            dstline += dst->stride;
        }
        break;
    case JBIG2_COMPOSE_XOR:
        for (j = 0; j < sh; j++) {
            for (i = 0; i < sw; i++) {
                image_set_pixel_unsafe(dst, dstline, i + x, j + y,
                                       image_get_pixel_unsafe(src, srcline, i + sx, j + sy) ^ image_get_pixel_unsafe(dst, dstline, i + x, j + y));
            }
            srcline += src->stride;
            dstline += dst->stride;
        }
        break;
    case JBIG2_COMPOSE_XNOR:
        for (j = 0; j < sh; j++) {
            for (i = 0; i < sw; i++) {
                image_set_pixel_unsafe(dst, dstline, i + x, j + y,
                                       (image_get_pixel_unsafe(src, srcline, i + sx, j + sy) == image_get_pixel_unsafe(dst, dstline, i + x, j + y)));
            }
            srcline += src->stride;
            dstline += dst->stride;
        }
        break;
    case JBIG2_COMPOSE_REPLACE:
        for (j = 0; j < sh; j++) {
            for (i = 0; i < sw; i++) {
                image_set_pixel_unsafe(dst, dstline, i + x, j + y, image_get_pixel_unsafe(src, srcline, i + sx, j + sy));
            }
            srcline += src->stride;
            dstline += dst->stride;
        }
        break;
    }

    return 0;
}

/* composite one jbig2_image onto another */
int
jbig2_image_compose(Jbig2Ctx *ctx, Jbig2Image *dst, Jbig2Image *src, int x, int y, Jbig2ComposeOp op)
{
    int i, j;
    int w, h;
    int leftbyte, rightbyte;
    int shift;
    uint8_t *s, *ss;
    uint8_t *d, *dd;
    uint8_t mask, rightmask;

    if (op != JBIG2_COMPOSE_OR && op != JBIG2_COMPOSE_REPLACE) {
        /* hand off the the general routine */
        return jbig2_image_compose_unopt(ctx, dst, src, x, y, op);
    }

    /* clip */
    w = src->width;
    h = src->height;
    ss = src->data;

    if (x < 0) {
        if ((x & 7) && op == JBIG2_COMPOSE_REPLACE) {
            // complete loop unswitch:
            // used a lot in jbig2_halftone and jbig2_symbol_dict
            // (often with small w)
            int sx = (-x) & 7;

            ss += (-x) >> 3;
            w += x;             /*x = 0; */
            if (y < 0) {
                ss += -y * src->stride;
                h += y;
                y = 0;
            }
            w = (w < dst->width) ? w : dst->width;
            h = (h < dst->height) ? h : dst->height;
            /* check for zero clipping region */
            if ((w <= 0) || (h <= 0))
                return 0;
            //assert(sx > 1 && sx < 8);
            s = ss;
            d = dd = dst->data + y * dst->stride;
            if (w < 8) {
                rightmask = (~0xff) >> (w & 7); // 0b11111000
                if (w <= 8 - sx) {
                    // d0 d1 d2 d3 d4.zz zz zz |
                    // xx xx s0 s1 s2 s3 s4 xx |
                    for (j = 0; j < h; j++) {
                        *d = (*d & ~rightmask) | (*s << sx);
                        s += src->stride;
                        d += dst->stride;
                    }
                } else {
                    // d0 d1 d2 d3 d4 d5.zz zz |
                    // xx xx xx xx s0 s1 s2 s3 | s4 s5 s6 s7 ...
                    // assert(w > 1);
                    for (j = 0; j < h; j++) {
                        *d &= ~rightmask;
                        *d |= ((s[0] << sx) | (s[1] >> (8 - sx))) & rightmask;
                        d = (dd += dst->stride);
                        s = (ss += src->stride);
                    }
                }
            } else if ((w & 7) == 0) {
                if (w == 8) {
                    // d0 d1 d2 d3 d4 d5 s6 s7 |
                    // xx xx xx xx s0 s1 s2 s3 | s4 s5 s6 s7 ...
                    for (j = 0; j < h; j++) {
                        *d = ((s[0] << sx) | (s[1] >> (8 - sx)));
                        d = (dd += dst->stride);
                        s = (ss += src->stride);
                    }
                } else {
                    // d0 d1 d2 d3 d4 d5 d6 d7 || d8 d9 d10 d11 d12 d13 d14 d15.|
                    // xx xx xx s0 s1 s2 s3 s4 || s5 s6  s7  s8  s9 s10 s11 s12 | s13 s14 s15.
                    rightbyte = w & ~7;
                    for (j = 0; j < h; j++) {
                        *d = *s++ << sx;
                        for (i = sx + 8; i < rightbyte; i += 8) {
                            *d++ |= *s >> (8 - sx);
                            *d = *s++ << sx;
                        }
                        *d++ |= *s >> (8 - sx);
                        //assert(d - dd == (w+7)>>3);
                        d = (dd += dst->stride);
                        s = (ss += src->stride);
                    }
                }
            } else {
                rightmask = (~0xff) >> (w & 7); // 0b11111000 | 0b1111100
                rightbyte = (w & ~7);
                if ((w & 7) <= 8 - sx) {
                    // d0 d1 d2 d3 d4 d5 d6 d7 || d8 d9 d10 d11.d12 d13 d14 d15 |
                    // xx xx xx s0 s1 s2 s3 s4 || s5 s6  s7+ s8  s9 s10 s11.s12 |
                    for (j = 0; j < h; j++) {
                        *d = *s++ << sx;
                        for (i = sx + 8; i < rightbyte; i += 8) {
                            *d++ |= *s >> (8 - sx);
                            *d = *s++ << sx;
                        }
                        *d++ |= *s >> (8 - sx);
                        *d &= ~rightmask;
                        *d++ |= ((s[0] << sx)) & rightmask;
                        //assert(d - dd == (w+7)>>3);
                        d = (dd += dst->stride);
                        s = (ss += src->stride);
                    }
                } else {
                    // d0 d1 d2 d3 d4 d5 d6 d7 || d8 d9 d10 d11 d12 d13.d14 d15 |
                    // xx xx xx s0 s1 s2 s3 s4 || s5 s6  s7+ s8  s9 s10 s11 s12.| s13
                    for (j = 0; j < h; j++) {
                        *d = *s++ << sx;
                        for (i = sx + 8; i < rightbyte; i += 8) {
                            *d++ |= *s >> (8 - sx);
                            *d = *s++ << sx;
                        }
                        *d++ |= *s >> (8 - sx);
                        *d &= ~rightmask;
                        *d++ |= ((s[0] << sx) | (s[1] >> (8 - sx))) & rightmask;
                        //assert(d - dd == (w+7)>>3);
                        d = (dd += dst->stride);
                        s = (ss += src->stride);
                    }
                }
            }
            return 0;
        }
        if ((x & 7)) {
            /* TODO NYI, fallback to unopt */
            return jbig2_image_compose_unopt(ctx, dst, src, x, y, op);
        }
        ss += (-x) >> 3;
        w += x;
        x = 0;
    }
    if (op == JBIG2_COMPOSE_REPLACE && (x & 7)) {
        /* NYI [***] */
        /* Note: jbig2_halftone and jbig2_symbol_dict only uses
         * negative x, this branch is only about positive x,
         * thus not worth optimizing
         */
        return jbig2_image_compose_unopt(ctx, dst, src, x, y, op);
    }
    if (y < 0) {
        ss += -y*src->stride;
        h += y;
        y = 0;
    }
    w = (x + w < dst->width) ? w : dst->width - x;
    h = (y + h < dst->height) ? h : dst->height - y;
#ifdef JBIG2_DEBUG
    jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, -1, "compositing %dx%d at (%d, %d) after clipping\n", w, h, x, y);
#endif

    /* check for zero clipping region */
    if ((w <= 0) || (h <= 0)) {
#ifdef JBIG2_DEBUG
        jbig2_error(ctx, JBIG2_SEVERITY_DEBUG, -1, "zero clipping region");
#endif
        return 0;
    }
#if 0
    /* special case complete/strip replacement */
    /* disabled because it's only safe to do when the destination
       buffer is all-blank. */
    if ((x == 0) && (w == src->width)) {
        memcpy(dst->data + y * dst->stride, src->data, h * src->stride);
        return 0;
    }
#endif

    leftbyte = x >> 3;
    rightbyte = (x + w - 1) >> 3;
    shift = x & 7;

    /* general OR case */
    s = ss;
    d = dd = dst->data + y * dst->stride + leftbyte;
    if (d < dst->data || leftbyte > dst->stride || h * dst->stride < 0 || d - leftbyte + h * dst->stride > dst->data + dst->height * dst->stride) {
        return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1, "preventing heap overflow in jbig2_image_compose");
    }
    if (op == JBIG2_COMPOSE_REPLACE) {
        uint8_t dmask;

        // complete unswitch loop:
        // used a lot in jbig2_halftone and jbig2_symbol_dict
        // (with negative x, when (-x) % 8 == 0)
        if (leftbyte == rightbyte) {
            mask = 0x100 - (0x100 >> w);
            dmask = ~(mask >> shift);
            for (j = 0; j < h; j++) {
                *d = (*d & dmask) | ((*s & mask) >> shift);
                d += dst->stride;
                s += src->stride;
            }
        } else if (shift == 0) {
            rightmask = (w & 7) ? 0x100 - (1 << (8 - (w & 7))) : 0xFF;
            for (j = 0; j < h; j++) {
                for (i = leftbyte; i < rightbyte; i++)
                    *d++ = *s++;
                *d = (*d & ~rightmask) | (*s & rightmask);
                d = (dd += dst->stride);
                s = (ss += src->stride);
            }
        } else {
            /* XXX NYI, should be fallbacked above [***] */
            return jbig2_error(ctx, JBIG2_SEVERITY_FATAL, -1,
                    "jbig2_image_compose(REPLACE): NYI");
        }
        return 0;
    }
    if (leftbyte == rightbyte) {
        mask = 0x100 - (0x100 >> w);
        for (j = 0; j < h; j++) {
            *d |= (*s & mask) >> shift;
            d += dst->stride;
            s += src->stride;
        }
    } else if (shift == 0) {
        rightmask = (w & 7) ? 0x100 - (1 << (8 - (w & 7))) : 0xFF;
        for (j = 0; j < h; j++) {
            for (i = leftbyte; i < rightbyte; i++)
                *d++ |= *s++;
            *d |= *s & rightmask;
            d = (dd += dst->stride);
            s = (ss += src->stride);
        }
    } else {
        bool overlap = (((w + 7) >> 3) < ((x + w + 7) >> 3) - (x >> 3));

        mask = 0x100 - (1 << shift);
        if (overlap)
            rightmask = (0x100 - (0x100 >> ((x + w) & 7))) >> (8 - shift);
        else
            rightmask = 0x100 - (0x100 >> (w & 7));
        for (j = 0; j < h; j++) {
            *d++ |= (*s & mask) >> shift;
            for (i = leftbyte; i < rightbyte - 1; i++) {
                *d |= ((*s++ & ~mask) << (8 - shift));
                *d++ |= ((*s & mask) >> shift);
            }
            if (overlap)
                *d |= (*s & rightmask) << (8 - shift);
            else
                *d |= ((s[0] & ~mask) << (8 - shift)) | ((s[1] & rightmask) >> shift);
            d = (dd += dst->stride);
            s = (ss += src->stride);
        }
    }

    return 0;
}

/* initialize an image bitmap to a constant value */
void
jbig2_image_clear(Jbig2Ctx *ctx, Jbig2Image *image, int value)
{
    const uint8_t fill = value ? 0xFF : 0x00;

    memset(image->data, fill, image->stride * image->height);
}

/* look up a pixel value in an image.
   returns 0 outside the image frame for the convenience of
   the template code
*/
int
jbig2_image_get_pixel(Jbig2Image *image, int x, int y)
{
    const int w = image->width;
    const int h = image->height;
    const int byte = (x >> 3) + y * image->stride;
    const int bit = 7 - (x & 7);

    if ((x < 0) || (x >= w))
        return 0;
    if ((y < 0) || (y >= h))
        return 0;

    return ((image->data[byte] >> bit) & 1);
}

/* set an individual pixel value in an image */
int
jbig2_image_set_pixel(Jbig2Image *image, int x, int y, bool value)
{
    const int w = image->width;
    const int h = image->height;
    int scratch, mask;
    int bit, byte;

    if ((x < 0) || (x >= w))
        return 0;
    if ((y < 0) || (y >= h))
        return 0;

    byte = (x >> 3) + y * image->stride;
    bit = 7 - (x & 7);
    mask = (1 << bit) ^ 0xff;

    scratch = image->data[byte] & mask;
    image->data[byte] = scratch | (value << bit);

    return 1;
}
