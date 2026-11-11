/*
 * Copyright (C) 2015-2022 libass contributors
 *
 * This file is part of libass.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define STRIPE_WIDTH  (ALIGNMENT / 2)
#define STRIPE_MASK   (STRIPE_WIDTH - 1)

inline static void SUFFIX(copy_line)(int16_t *buf, const int16_t *ptr, size_t offs, size_t size)
{
    memcpy(buf, get_line(ptr, offs, size), STRIPE_WIDTH * sizeof(buf[0]));
}

#define copy_line  SUFFIX(copy_line)

/*
 * Stremio Origin: the two main parametric filters' inner loops in NEON where libass has no assembly of its own
 * (32-bit ARM TVs, where the blurs were most of a styled frame). The same integer steps as the C below, lane for
 * lane: the differences wrap to 16 bits, the products add up in 32, the sum is shifted down 16 and added back with
 * 16-bit wrap: the result is bit for bit the C one (tests checked it on the TV and the Mac). On 32-bit ARM it is
 * inline assembly (this compiler's soft-float NEON intrinsics crash it), used only when the CPU says it has NEON.
 */
#undef SB_BLUR_NEON
#undef SB_BLUR_STRIPE
#if STRIPE_WIDTH == 8 && defined(__arm__) && !defined(__aarch64__)
#ifndef SB_NEON_PROBE
#define SB_NEON_PROBE
#include <stdio.h>
#include <string.h>
static int sb_neon_have(void) /* from the kernel's own list of the CPU's features, once */
{
    static int have = -1;
    if (have < 0) { have = 0; FILE *f = fopen("/proc/cpuinfo", "r"); char l[512];
        while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "Features", 8) && strstr(l, " neon")) { have = 1; break; }
        if (f) fclose(f); }
    return have;
}
#endif
#define SB_BLUR_NEON sb_neon_have()
#define SB_BLUR_STRIPE(dst, c_, a_, b_, param, n) do { \
        const int16_t *ap_[8], *bp_[8]; int16_t pr_[8]; int k_ = 0; \
        for (int i = (n); i > 0; i--, k_++) { ap_[k_] = (a_); bp_[k_] = (b_); pr_[k_] = (param)[i - 1]; } \
        const int16_t *const *pa_ = ap_, *const *pb_ = bp_; const int16_t *pp_ = pr_; int left_ = (n); const int16_t *pc_ = (c_); int16_t *pd_ = (dst); \
        const int16_t *t0_, *t1_; \
        __asm__ volatile(".fpu neon\n" \
            "vld1.16 {d16-d17}, [%[c]]\n" "vmov.i32 q9, #0x8000\n" "vmov.i32 q10, #0x8000\n" \
            "1:\n" "ldr %[t0], [%[pa]], #4\n" "ldr %[t1], [%[pb]], #4\n" \
            "vld1.16 {d0-d1}, [%[t0]]\n" "vld1.16 {d2-d3}, [%[t1]]\n" "vld1.16 {d4[]}, [%[pp]]!\n" \
            "vsub.i16 q0, q0, q8\n" "vsub.i16 q1, q1, q8\n" \
            "vmlal.s16 q9, d0, d4[0]\n" "vmlal.s16 q10, d1, d4[0]\n" "vmlal.s16 q9, d2, d4[0]\n" "vmlal.s16 q10, d3, d4[0]\n" \
            "subs %[left], %[left], #1\n" "bne 1b\n" \
            "vshrn.i32 d0, q9, #16\n" "vshrn.i32 d1, q10, #16\n" "vadd.i16 q0, q0, q8\n" "vst1.16 {d0-d1}, [%[d]]\n" \
            : [pa] "+r" (pa_), [pb] "+r" (pb_), [pp] "+r" (pp_), [left] "+r" (left_), [t0] "=&r" (t0_), [t1] "=&r" (t1_) \
            : [c] "r" (pc_), [d] "r" (pd_) \
            : "d0", "d1", "d2", "d3", "d4", "d5", "d16", "d17", "d18", "d19", "d20", "d21", "memory", "cc"); } while (0)
#elif STRIPE_WIDTH == 8 && defined(__ARM_NEON) && defined(__aarch64__)
#include <arm_neon.h>
#define SB_BLUR_NEON 1
#define SB_BLUR_STRIPE(dst, c_, a_, b_, param, n) do { \
        int16x8_t c8 = vld1q_s16(c_); int32x4_t lo = vdupq_n_s32(0x8000), hi = lo; \
        for (int i = (n); i > 0; i--) { \
            int16_t pp = (param)[i - 1]; \
            int16x8_t d1 = vsubq_s16(vld1q_s16(a_), c8), d2 = vsubq_s16(vld1q_s16(b_), c8); \
            lo = vmlal_n_s16(lo, vget_low_s16(d1), pp); hi = vmlal_n_s16(hi, vget_high_s16(d1), pp); \
            lo = vmlal_n_s16(lo, vget_low_s16(d2), pp); hi = vmlal_n_s16(hi, vget_high_s16(d2), pp); } \
        vst1q_s16(dst, vaddq_s16(c8, vcombine_s16(vshrn_n_s32(lo, 16), vshrn_n_s32(hi, 16)))); } while (0)
#else
#define SB_BLUR_NEON 0
#endif

/*
 * Unpack/Pack Functions
 *
 * Convert between regular 8-bit bitmap and internal format.
 * Internal image is stored as set of vertical stripes of size [STRIPE_WIDTH x height].
 * Each pixel is represented as 16-bit integer in range of [0-0x4000].
 */

void SUFFIX(ass_stripe_unpack)(int16_t *restrict dst, const uint8_t *restrict src,
                               ptrdiff_t src_stride, size_t width, size_t height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT) && !(src_stride % ALIGNMENT));
    ASSUME(width > 0 && height > 0);

    for (size_t y = 0; y < height; y++) {
        int16_t *ptr = dst;
        for (size_t x = 0; x < width; x += STRIPE_WIDTH) {
            for (int k = 0; k < STRIPE_WIDTH; k++)
                ptr[k] = (uint16_t) (((src[x + k] << 7) | (src[x + k] >> 1)) + 1) >> 1;
                //ptr[k] = (0x4000 * src[x + k] + 127) / 255;
            ptr += STRIPE_WIDTH * height;
        }
        dst += STRIPE_WIDTH;
        src += src_stride;
    }
}

void SUFFIX(ass_stripe_pack)(uint8_t *restrict dst, ptrdiff_t dst_stride,
                             const int16_t *restrict src, size_t width, size_t height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT) && !(dst_stride % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(width > 0 && height > 0);

    for (size_t x = 0; x < width; x += STRIPE_WIDTH) {
        uint8_t *ptr = dst;
        for (size_t y = 0; y < height; y++) {
            const int16_t *dither = dither_line + 16 * (y & 1);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                ptr[k] = (uint16_t) (src[k] - (src[k] >> 8) + dither[k]) >> 6;
                //ptr[k] = (255 * src[k] + 0x1FFF) / 0x4000;
            ptr += dst_stride;
            src += STRIPE_WIDTH;
        }
        dst += STRIPE_WIDTH;
    }
    size_t left = dst_stride - ((width + STRIPE_MASK) & ~STRIPE_MASK);
    for (size_t y = 0; y < height; y++) {
        for (size_t x = 0; x < left; x++)
            dst[x] = 0;
        dst += dst_stride;
    }
}

/*
 * Contract Filters
 *
 * Contract image by factor 2 with kernel [1, 5, 10, 10, 5, 1].
 */

void SUFFIX(ass_shrink_horz)(int16_t *restrict dst, const int16_t *restrict src,
                             size_t src_width, size_t src_height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_width = (src_width + 5) >> 1;
    size_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    size_t step = STRIPE_WIDTH * src_height;

    size_t offs = 0;
    int16_t buf[3 * STRIPE_WIDTH];
    int16_t *ptr = buf + STRIPE_WIDTH;
    for (size_t x = 0; x < dst_width; x += STRIPE_WIDTH) {
        for (size_t y = 0; y < src_height; y++) {
            copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
            copy_line(ptr + 0 * STRIPE_WIDTH, src, offs + 0 * step, size);
            copy_line(ptr + 1 * STRIPE_WIDTH, src, offs + 1 * step, size);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = shrink_func(ptr[2 * k - 4], ptr[2 * k - 3],
                                     ptr[2 * k - 2], ptr[2 * k - 1],
                                     ptr[2 * k + 0], ptr[2 * k + 1]);
            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        offs += step;
    }
}

void SUFFIX(ass_shrink_vert)(int16_t *restrict dst, const int16_t *restrict src,
                             size_t src_width, size_t src_height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_height = (src_height + 5) >> 1;
    size_t step = STRIPE_WIDTH * src_height;

    for (size_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y++) {
            const int16_t *p1p = get_line(src, offs - 4 * STRIPE_WIDTH, step);
            const int16_t *p1n = get_line(src, offs - 3 * STRIPE_WIDTH, step);
            const int16_t *z0p = get_line(src, offs - 2 * STRIPE_WIDTH, step);
            const int16_t *z0n = get_line(src, offs - 1 * STRIPE_WIDTH, step);
            const int16_t *n1p = get_line(src, offs - 0 * STRIPE_WIDTH, step);
            const int16_t *n1n = get_line(src, offs + 1 * STRIPE_WIDTH, step);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = shrink_func(p1p[k], p1n[k], z0p[k], z0n[k], n1p[k], n1n[k]);
            dst  += 1 * STRIPE_WIDTH;
            offs += 2 * STRIPE_WIDTH;
        }
        src += step;
    }
}

/*
 * Expand Filters
 *
 * Expand image by factor 2 with kernel [5, 10, 1], [1, 10, 5].
 */

void SUFFIX(ass_expand_horz)(int16_t *restrict dst, const int16_t *restrict src,
                             size_t src_width, size_t src_height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_width = 2 * src_width + 4;
    size_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    size_t step = STRIPE_WIDTH * src_height;

    size_t offs = 0;
    int16_t buf[2 * STRIPE_WIDTH];
    int16_t *ptr = buf + STRIPE_WIDTH;
    for (size_t x = STRIPE_WIDTH; x < dst_width; x += 2 * STRIPE_WIDTH) {
        for (size_t y = 0; y < src_height; y++) {
            copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
            copy_line(ptr - 0 * STRIPE_WIDTH, src, offs - 0 * step, size);
            for (int k = 0; k < STRIPE_WIDTH / 2; k++)
                expand_func(&dst[2 * k], &dst[2 * k + 1],
                            ptr[k - 2], ptr[k - 1], ptr[k]);
            int16_t *next = dst + step - STRIPE_WIDTH;
            for (int k = STRIPE_WIDTH / 2; k < STRIPE_WIDTH; k++)
                expand_func(&next[2 * k], &next[2 * k + 1],
                            ptr[k - 2], ptr[k - 1], ptr[k]);
            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        dst += step;
    }
    if ((dst_width - 1) & STRIPE_WIDTH)
        return;

    for (size_t y = 0; y < src_height; y++) {
        copy_line(ptr - 1 * STRIPE_WIDTH, src, offs - 1 * step, size);
        copy_line(ptr - 0 * STRIPE_WIDTH, src, offs - 0 * step, size);
        for (int k = 0; k < STRIPE_WIDTH / 2; k++)
            expand_func(&dst[2 * k], &dst[2 * k + 1],
                        ptr[k - 2], ptr[k - 1], ptr[k]);
        dst  += STRIPE_WIDTH;
        offs += STRIPE_WIDTH;
    }
}

void SUFFIX(ass_expand_vert)(int16_t *restrict dst, const int16_t *restrict src,
                             size_t src_width, size_t src_height)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_height = 2 * src_height + 4;
    size_t step = STRIPE_WIDTH * src_height;

    for (size_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y += 2) {
            const int16_t *p1 = get_line(src, offs - 2 * STRIPE_WIDTH, step);
            const int16_t *z0 = get_line(src, offs - 1 * STRIPE_WIDTH, step);
            const int16_t *n1 = get_line(src, offs - 0 * STRIPE_WIDTH, step);
            for (int k = 0; k < STRIPE_WIDTH; k++)
                expand_func(&dst[k], &dst[k + STRIPE_WIDTH],
                            p1[k], z0[k], n1[k]);
            dst  += 2 * STRIPE_WIDTH;
            offs += 1 * STRIPE_WIDTH;
        }
        src += step;
    }
}

/*
 * Main Parametric Filters
 *
 * Perform 1D convolution with kernel [..., c2, c1, c0, d, c0, c1, c2, ...],
 * cN = param[N], d = 1 - 2 * (c0 + c1 + c2 + ...),
 * number of parameters is part of the function name.
 */

static inline void SUFFIX(blur_horz)(int16_t *restrict dst, const int16_t *restrict src,
                                     size_t src_width, size_t src_height,
                                     const int16_t *restrict param, const int n)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_width = src_width + 2 * n;
    size_t size = ((src_width + STRIPE_MASK) & ~STRIPE_MASK) * src_height;
    size_t step = STRIPE_WIDTH * src_height;

    size_t offs = 0;
    int16_t buf[3 * STRIPE_WIDTH];
    int16_t *ptr = buf + 2 * STRIPE_WIDTH;
    for (size_t x = 0; x < dst_width; x += STRIPE_WIDTH) {
        for (size_t y = 0; y < src_height; y++) {
            for (int i = -((2 * n + STRIPE_WIDTH - 1u) / STRIPE_WIDTH); i <= 0; i++)
                copy_line(ptr + i * STRIPE_WIDTH, src, offs + i * step, size);
#if defined(SB_BLUR_STRIPE)
            if (SB_BLUR_NEON) SB_BLUR_STRIPE(dst, ptr - n, ptr - n - i, ptr - n + i, param, n); else {
#else
            {
#endif
            int32_t acc[STRIPE_WIDTH];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                acc[k] = 0x8000;
            for (int i = n; i > 0; i--)
                for (int k = 0; k < STRIPE_WIDTH; k++)
                    acc[k] += (int16_t) (ptr[k - n - i] - ptr[k - n]) * param[i - 1] +
                              (int16_t) (ptr[k - n + i] - ptr[k - n]) * param[i - 1];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = ptr[k - n] + (acc[k] >> 16);
            }

            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
    }
}

static inline void SUFFIX(blur_vert)(int16_t *restrict dst, const int16_t *restrict src,
                                     size_t src_width, size_t src_height,
                                     const int16_t *restrict param, const int n)
{
    ASSUME(!((uintptr_t) dst % ALIGNMENT));
    ASSUME(!((uintptr_t) src % ALIGNMENT));
    ASSUME(src_width > 0 && src_height > 0);

    size_t dst_height = src_height + 2 * n;
    size_t step = STRIPE_WIDTH * src_height;

    for (size_t x = 0; x < src_width; x += STRIPE_WIDTH) {
        size_t offs = 0;
        for (size_t y = 0; y < dst_height; y++) {
#if defined(SB_BLUR_STRIPE)
            if (SB_BLUR_NEON) { const int16_t *center_ = get_line(src, offs - n * STRIPE_WIDTH, step);
                SB_BLUR_STRIPE(dst, center_, get_line(src, offs - (n + i) * STRIPE_WIDTH, step), get_line(src, offs - (n - i) * STRIPE_WIDTH, step), param, n); } else {
#else
            {
#endif
            int32_t acc[STRIPE_WIDTH];
            for (int k = 0; k < STRIPE_WIDTH; k++)
                acc[k] = 0x8000;
            const int16_t *center = get_line(src, offs - n * STRIPE_WIDTH, step);
            for (int i = n; i > 0; i--) {
                const int16_t *line1 = get_line(src, offs - (n + i) * STRIPE_WIDTH, step);
                const int16_t *line2 = get_line(src, offs - (n - i) * STRIPE_WIDTH, step);
                for (int k = 0; k < STRIPE_WIDTH; k++)
                    acc[k] += (int16_t) (line1[k] - center[k]) * param[i - 1] +
                              (int16_t) (line2[k] - center[k]) * param[i - 1];
            }
            for (int k = 0; k < STRIPE_WIDTH; k++)
                dst[k] = center[k] + (acc[k] >> 16);
            }

            dst  += STRIPE_WIDTH;
            offs += STRIPE_WIDTH;
        }
        src += step;
    }
}

void SUFFIX(ass_blur4_horz)(int16_t *restrict dst, const int16_t *restrict src,
                            size_t src_width, size_t src_height,
                            const int16_t *restrict param)
{
    SUFFIX(blur_horz)(dst, src, src_width, src_height, param, 4);
}

void SUFFIX(ass_blur4_vert)(int16_t *restrict dst, const int16_t *restrict src,
                            size_t src_width, size_t src_height,
                            const int16_t *restrict param)
{
    SUFFIX(blur_vert)(dst, src, src_width, src_height, param, 4);
}

void SUFFIX(ass_blur5_horz)(int16_t *restrict dst, const int16_t *restrict src,
                            size_t src_width, size_t src_height,
                            const int16_t *restrict param)
{
    SUFFIX(blur_horz)(dst, src, src_width, src_height, param, 5);
}

void SUFFIX(ass_blur5_vert)(int16_t *dst, const int16_t *src,
                            size_t src_width, size_t src_height,
                            const int16_t *param)
{
    SUFFIX(blur_vert)(dst, src, src_width, src_height, param, 5);
}

void SUFFIX(ass_blur6_horz)(int16_t *dst, const int16_t *src,
                            size_t src_width, size_t src_height,
                            const int16_t *param)
{
    SUFFIX(blur_horz)(dst, src, src_width, src_height, param, 6);
}

void SUFFIX(ass_blur6_vert)(int16_t *dst, const int16_t *src,
                            size_t src_width, size_t src_height,
                            const int16_t *param)
{
    SUFFIX(blur_vert)(dst, src, src_width, src_height, param, 6);
}

void SUFFIX(ass_blur7_horz)(int16_t *dst, const int16_t *src,
                            size_t src_width, size_t src_height,
                            const int16_t *param)
{
    SUFFIX(blur_horz)(dst, src, src_width, src_height, param, 7);
}

void SUFFIX(ass_blur7_vert)(int16_t *dst, const int16_t *src,
                      size_t src_width, size_t src_height,
                      const int16_t *param)
{
    SUFFIX(blur_vert)(dst, src, src_width, src_height, param, 7);
}

void SUFFIX(ass_blur8_horz)(int16_t *dst, const int16_t *src,
                      size_t src_width, size_t src_height,
                      const int16_t *param)
{
    SUFFIX(blur_horz)(dst, src, src_width, src_height, param, 8);
}

void SUFFIX(ass_blur8_vert)(int16_t *dst, const int16_t *src,
                      size_t src_width, size_t src_height,
                      const int16_t *param)
{
    SUFFIX(blur_vert)(dst, src, src_width, src_height, param, 8);
}


#undef STRIPE_WIDTH
#undef STRIPE_MASK
#undef copy_line
