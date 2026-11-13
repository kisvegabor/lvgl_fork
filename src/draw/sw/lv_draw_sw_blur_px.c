/**
 * @file lv_draw_sw_blur_px.c
 *
 * The functions of the blur that work on the pixels of a line, for each color format.
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_sw_blur_private.h"

#if LV_USE_DRAW_SW

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

static void blur_line_1_byte(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity);
static void blur_line_2_bytes(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity);
static void blur_line_2_bytes_swapped(void * buf, int32_t step, int32_t cnt, uint32_t sample_len,
                                      uint32_t intensity);
static void blur_line_3_bytes(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity);
static inline void blur_line_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, uint32_t sample_len,
                                          uint32_t intensity, bool swapped);

static void smooth_1_byte(void * buf, int32_t step, int32_t cnt);
static void smooth_2_bytes(void * buf, int32_t step, int32_t cnt);
static void smooth_2_bytes_swapped(void * buf, int32_t step, int32_t cnt);
static void smooth_3_bytes(void * buf, int32_t step, int32_t cnt);
static void smooth_4_bytes(void * buf, int32_t step, int32_t cnt);
static inline void smooth_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, bool swapped);

static void upsample_1_byte(void * buf, int32_t step, int32_t cnt);
static void upsample_2_bytes(void * buf, int32_t step, int32_t cnt);
static void upsample_2_bytes_swapped(void * buf, int32_t step, int32_t cnt);
static void upsample_3_bytes(void * buf, int32_t step, int32_t cnt);
static void upsample_4_bytes(void * buf, int32_t step, int32_t cnt);
static inline void upsample_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, bool swapped);

static void rows_1_byte(void * dest, const void * above, const void * below, int32_t step, int32_t cnt, bool smooth);
static void rows_2_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth);
static void rows_2_bytes_swapped(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                                 bool smooth);
static void rows_3_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth);
static void rows_4_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth);
static inline void rows_2_bytes_core(uint16_t * dest, const uint16_t * above, const uint16_t * below, int32_t step,
                                     int32_t cnt, bool smooth, bool swapped);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

const lv_draw_sw_blur_format_t * lv_draw_sw_blur_get_format(lv_color_format_t cf)
{
    static const lv_draw_sw_blur_format_t format_1_byte = {
        blur_line_1_byte, smooth_1_byte, upsample_1_byte, rows_1_byte, AVG_MASK_8, 0, false, 1
    };
    static const lv_draw_sw_blur_format_t format_al88 = {
        blur_line_1_byte, smooth_1_byte, upsample_1_byte, rows_1_byte, AVG_MASK_8, ALPHA_MASK_AL88, false, 1
    };
    static const lv_draw_sw_blur_format_t format_2_bytes = {
        blur_line_2_bytes, smooth_2_bytes, upsample_2_bytes, rows_2_bytes, AVG_MASK_16, 0, false, 2
    };
    static const lv_draw_sw_blur_format_t format_2_bytes_swapped = {
        blur_line_2_bytes_swapped, smooth_2_bytes_swapped, upsample_2_bytes_swapped, rows_2_bytes_swapped,
        AVG_MASK_16, 0, true, 2
    };
    static const lv_draw_sw_blur_format_t format_3_bytes = {
        blur_line_3_bytes, smooth_3_bytes, upsample_3_bytes, rows_3_bytes, AVG_MASK_8, 0, false, 3
    };
    static const lv_draw_sw_blur_format_t format_xrgb8888 = {
        blur_line_3_bytes, smooth_4_bytes, upsample_4_bytes, rows_4_bytes, AVG_MASK_8, 0, false, 4
    };
    static const lv_draw_sw_blur_format_t format_argb8888 = {
        blur_line_3_bytes, smooth_4_bytes, upsample_4_bytes, rows_4_bytes, AVG_MASK_8, ~COLOR_MASK_32, false, 3
    };

    switch(cf) {
        case LV_COLOR_FORMAT_L8:
        case LV_COLOR_FORMAT_A8:
            return &format_1_byte;
        case LV_COLOR_FORMAT_AL88:
            return &format_al88;
        case LV_COLOR_FORMAT_RGB565:
            return &format_2_bytes;
        case LV_COLOR_FORMAT_RGB565_SWAPPED:
            return &format_2_bytes_swapped;
        case LV_COLOR_FORMAT_RGB888:
            return &format_3_bytes;
        case LV_COLOR_FORMAT_XRGB8888:
            return &format_xrgb8888;
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED:
            return &format_argb8888;
        default:
            return NULL;
    }
}

void lv_draw_sw_blur_px_double(uint8_t * dest_last, const uint8_t * src_last, int32_t px_size, int32_t cnt,
                               uint32_t size)
{
    if(size == 2 && px_size == 2) {
        uint16_t * dest16 = (uint16_t *)dest_last;
        const uint16_t * src16 = (const uint16_t *)src_last;
        for(; cnt > 0; cnt--) {
            uint16_t px = *src16;
            dest16[0] = px;
            dest16[1] = px;
            dest16 -= 2;
            src16--;
        }
    }
    else if(size == 4) {
        uint32_t * dest32 = (uint32_t *)dest_last;
        const uint32_t * src32 = (const uint32_t *)src_last;
        for(; cnt > 0; cnt--) {
            uint32_t px = *src32;
            dest32[0] = px;
            dest32[1] = px;
            dest32 -= 2;
            src32--;
        }
    }
    else {
        for(; cnt > 0; cnt--) {
            uint8_t px0 = src_last[0];
            dest_last[0] = px0;
            dest_last[px_size] = px0;
            if(size == 3) {
                uint8_t px1 = src_last[1];
                uint8_t px2 = src_last[2];
                dest_last[1] = px1;
                dest_last[2] = px2;
                dest_last[px_size + 1] = px1;
                dest_last[px_size + 2] = px2;
            }
            dest_last -= 2 * px_size;
            src_last -= px_size;
        }
    }
}

void lv_draw_sw_blur_px_copy(uint8_t * dest, const uint8_t * src, int32_t dest_step, int32_t src_step, int32_t cnt,
                             uint32_t size)
{
    if(dest_step == (int32_t)size && src_step == (int32_t)size) {
        lv_memcpy(dest, src, cnt * size);
    }
    else if(size == 2) {
        for(; cnt > 0; cnt--) {
            *(uint16_t *)dest = *(const uint16_t *)src;
            dest += dest_step;
            src += src_step;
        }
    }
    else if(size == 4) {
        for(; cnt > 0; cnt--) {
            *(uint32_t *)dest = *(const uint32_t *)src;
            dest += dest_step;
            src += src_step;
        }
    }
    else {
        for(; cnt > 0; cnt--) {
            dest[0] = src[0];
            if(size == 3) {
                dest[1] = src[1];
                dest[2] = src[2];
            }
            dest += dest_step;
            src += src_step;
        }
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void blur_line_1_byte(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity)
{
    uint8_t * buf8 = buf;
    uint32_t intensity_inv = BLUR_INTENSITY_MAX - intensity;
    uint32_t half = intensity_inv / 2;

    uint32_t sum = 0;
    uint32_t s;
    for(s = 0; s < sample_len; s++) {
        sum += buf8[(int32_t)s * step];
    }
    sum = (sum << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;

    for(; cnt > 0; cnt--) {
        uint32_t px = *buf8;
        if(px != (sum >> BLUR_INTENSITY_BITS)) {
            sum = ((sum * intensity) >> BLUR_INTENSITY_BITS) + px * intensity_inv + half;
            *buf8 = sum >> BLUR_INTENSITY_BITS;
        }
        buf8 += step;
    }
}

static void blur_line_2_bytes(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity)
{
    blur_line_2_bytes_core(buf, step / 2, cnt, sample_len, intensity, false);
}

static void blur_line_2_bytes_swapped(void * buf, int32_t step, int32_t cnt, uint32_t sample_len,
                                      uint32_t intensity)
{
    blur_line_2_bytes_core(buf, step / 2, cnt, sample_len, intensity, true);
}

static void blur_line_3_bytes(void * buf, int32_t step, int32_t cnt, uint32_t sample_len, uint32_t intensity)
{
    /*Compiler optimization might mishandle it, so add volatile*/
    volatile uint8_t * buf8 = buf;
    uint32_t intensity_inv = BLUR_INTENSITY_MAX - intensity;
    uint32_t half = intensity_inv / 2;

    uint32_t sum0 = 0;
    uint32_t sum1 = 0;
    uint32_t sum2 = 0;
    uint32_t s;
    for(s = 0; s < sample_len; s++) {
        volatile uint8_t * sample = &buf8[(int32_t)s * step];
        sum0 += sample[0];
        sum1 += sample[1];
        sum2 += sample[2];
    }
    sum0 = (sum0 << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;
    sum1 = (sum1 << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;
    sum2 = (sum2 << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;

    while(cnt > 0) {
        /*Go quickly through the pixels that already equal the result*/
        while(buf8[0] == (sum0 >> BLUR_INTENSITY_BITS) && buf8[1] == (sum1 >> BLUR_INTENSITY_BITS) &&
              buf8[2] == (sum2 >> BLUR_INTENSITY_BITS)) {
            cnt--;
            if(cnt == 0) return;
            buf8 += step;
        }

        sum0 = ((sum0 * intensity) >> BLUR_INTENSITY_BITS) + buf8[0] * intensity_inv + half;
        buf8[0] = sum0 >> BLUR_INTENSITY_BITS;

        sum1 = ((sum1 * intensity) >> BLUR_INTENSITY_BITS) + buf8[1] * intensity_inv + half;
        buf8[1] = sum1 >> BLUR_INTENSITY_BITS;

        sum2 = ((sum2 * intensity) >> BLUR_INTENSITY_BITS) + buf8[2] * intensity_inv + half;
        buf8[2] = sum2 >> BLUR_INTENSITY_BITS;

        buf8 += step;
        cnt--;
    }
}

static inline void blur_line_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, uint32_t sample_len,
                                          uint32_t intensity, bool swapped)
{
    uint32_t intensity_inv = BLUR_INTENSITY_MAX - intensity;
    uint32_t half = intensity_inv / 2;

    uint32_t sum_r = 0;
    uint32_t sum_g = 0;
    uint32_t sum_b = 0;
    uint32_t s;
    for(s = 0; s < sample_len; s++) {
        uint32_t px = buf[(int32_t)s * step];
        if(swapped) px = lv_color_swap_16(px);
        sum_r += px >> 11;
        sum_g += (px >> 5) & 0x3F;
        sum_b += px & 0x1F;
    }
    sum_r = (sum_r << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;
    sum_g = (sum_g << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;
    sum_b = (sum_b << BLUR_INTENSITY_BITS) / sample_len + BLUR_INTENSITY_HALF;

    /*The result of the filter as a pixel*/
    uint32_t px_res = ((sum_r >> BLUR_INTENSITY_BITS) << 11) | ((sum_g >> BLUR_INTENSITY_BITS) << 5) |
                      (sum_b >> BLUR_INTENSITY_BITS);
    if(swapped) px_res = lv_color_swap_16(px_res);

    while(cnt > 0) {
        /*Go quickly through the pixels that already equal the result*/
        uint32_t px = *buf;
        while(px == px_res) {
            cnt--;
            if(cnt == 0) return;
            buf += step;
            px = *buf;
        }

        if(swapped) px = lv_color_swap_16(px);

        sum_r = ((sum_r * intensity) >> BLUR_INTENSITY_BITS) + (px >> 11) * intensity_inv + half;
        sum_g = ((sum_g * intensity) >> BLUR_INTENSITY_BITS) + ((px >> 5) & 0x3F) * intensity_inv + half;
        sum_b = ((sum_b * intensity) >> BLUR_INTENSITY_BITS) + (px & 0x1F) * intensity_inv + half;

        px_res = ((sum_r >> BLUR_INTENSITY_BITS) << 11) | ((sum_g >> BLUR_INTENSITY_BITS) << 5) |
                 (sum_b >> BLUR_INTENSITY_BITS);
        if(swapped) px_res = lv_color_swap_16(px_res);

        *buf = px_res;
        buf += step;
        cnt--;
    }
}

/*In the smooth functions the first and the last pixel have one neighbor. It is used for both sides.*/

static void smooth_1_byte(void * buf, int32_t step, int32_t cnt)
{
    uint8_t * px = buf;
    if(cnt < 2) return;

    int32_t i;
    for(i = 0; i < cnt; i += 2) {
        uint32_t prev = i > 0 ? px[-step] : px[step];
        uint32_t next = i + 1 < cnt ? px[step] : prev;
        px[0] = TENT(prev, px[0], next, AVG_MASK_8);
        px += 2 * step;
    }
}

static void smooth_2_bytes(void * buf, int32_t step, int32_t cnt)
{
    smooth_2_bytes_core(buf, step / 2, cnt, false);
}

static void smooth_2_bytes_swapped(void * buf, int32_t step, int32_t cnt)
{
    smooth_2_bytes_core(buf, step / 2, cnt, true);
}

static inline void smooth_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, bool swapped)
{
    if(cnt < 2) return;

    /*The `next` of a pixel is the `prev` of the next one*/
    uint32_t prev = buf[step];
    if(swapped) prev = lv_color_swap_16(prev);

    int32_t i;
    for(i = 0; i < cnt; i += 2) {
        uint32_t px = buf[0];
        uint32_t next = prev;
        if(i + 1 < cnt) {
            next = buf[step];
            if(swapped) next = lv_color_swap_16(next);
        }
        if(swapped) px = lv_color_swap_16(px);
        px = TENT(prev, px, next, AVG_MASK_16);
        if(swapped) px = lv_color_swap_16(px);
        buf[0] = px;
        prev = next;
        buf += 2 * step;
    }
}

static void smooth_3_bytes(void * buf, int32_t step, int32_t cnt)
{
    uint8_t * px = buf;
    if(cnt < 2) return;

    int32_t i;
    for(i = 0; i < cnt; i += 2) {
        const uint8_t * prev = i > 0 ? px - step : px + step;
        const uint8_t * next = i + 1 < cnt ? px + step : prev;
        px[0] = TENT(prev[0], px[0], next[0], AVG_MASK_8);
        px[1] = TENT(prev[1], px[1], next[1], AVG_MASK_8);
        px[2] = TENT(prev[2], px[2], next[2], AVG_MASK_8);
        px += 2 * step;
    }
}

static void smooth_4_bytes(void * buf, int32_t step, int32_t cnt)
{
    uint32_t * px = buf;
    step /= 4;
    if(cnt < 2) return;

    int32_t i;
    for(i = 0; i < cnt; i += 2) {
        uint32_t prev = i > 0 ? px[-step] : px[step];
        uint32_t next = i + 1 < cnt ? px[step] : prev;
        px[0] = (TENT(prev, px[0], next, AVG_MASK_8) & COLOR_MASK_32) | (px[0] & ~COLOR_MASK_32);
        px += 2 * step;
    }
}

/*In the upsample functions the last pixel can be without a next one. It is a copy of the previous one.*/

static void upsample_1_byte(void * buf, int32_t step, int32_t cnt)
{
    uint8_t * px = buf;
    int32_t i;
    for(i = 1; i + 1 < cnt; i += 2) {
        px[step] = (px[0] + px[2 * step]) >> 1;
        px += 2 * step;
    }
    if(i < cnt) px[step] = px[0];
}

static void upsample_2_bytes(void * buf, int32_t step, int32_t cnt)
{
    upsample_2_bytes_core(buf, step / 2, cnt, false);
}

static void upsample_2_bytes_swapped(void * buf, int32_t step, int32_t cnt)
{
    upsample_2_bytes_core(buf, step / 2, cnt, true);
}

static inline void upsample_2_bytes_core(uint16_t * buf, int32_t step, int32_t cnt, bool swapped)
{
    uint32_t prev = buf[0];
    if(swapped) prev = lv_color_swap_16(prev);

    int32_t i;
    for(i = 1; i + 1 < cnt; i += 2) {
        uint32_t next = buf[2 * step];
        if(swapped) next = lv_color_swap_16(next);
        uint32_t px = AVG_FLOOR(prev, next, AVG_MASK_16);
        if(swapped) px = lv_color_swap_16(px);
        buf[step] = px;
        prev = next;
        buf += 2 * step;
    }
    if(i < cnt) buf[step] = buf[0];
}

static void upsample_3_bytes(void * buf, int32_t step, int32_t cnt)
{
    uint8_t * px = buf;
    int32_t i;
    for(i = 1; i + 1 < cnt; i += 2) {
        const uint8_t * next = px + 2 * step;
        px[step + 0] = (px[0] + next[0]) >> 1;
        px[step + 1] = (px[1] + next[1]) >> 1;
        px[step + 2] = (px[2] + next[2]) >> 1;
        px += 2 * step;
    }
    if(i < cnt) {
        px[step + 0] = px[0];
        px[step + 1] = px[1];
        px[step + 2] = px[2];
    }
}

static void upsample_4_bytes(void * buf, int32_t step, int32_t cnt)
{
    uint32_t * px = buf;
    step /= 4;

    uint32_t prev = px[0];
    int32_t i;
    for(i = 1; i + 1 < cnt; i += 2) {
        uint32_t next = px[2 * step];
        px[step] = (AVG_FLOOR(prev, next, AVG_MASK_8) & COLOR_MASK_32) | (px[step] & ~COLOR_MASK_32);
        prev = next;
        px += 2 * step;
    }
    if(i < cnt) px[step] = (prev & COLOR_MASK_32) | (px[step] & ~COLOR_MASK_32);
}

static void rows_1_byte(void * dest, const void * above, const void * below, int32_t step, int32_t cnt, bool smooth)
{
    uint8_t * px = dest;
    const uint8_t * px_above = above;
    const uint8_t * px_below = below;
    for(; cnt > 0; cnt--) {
        if(smooth) px[0] = TENT(px_above[0], px[0], px_below[0], AVG_MASK_8);
        else px[0] = (px_above[0] + px_below[0]) >> 1;
        px += step;
        px_above += step;
        px_below += step;
    }
}

static void rows_2_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth)
{
    rows_2_bytes_core(dest, above, below, step / 2, cnt, smooth, false);
}

static void rows_2_bytes_swapped(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                                 bool smooth)
{
    rows_2_bytes_core(dest, above, below, step / 2, cnt, smooth, true);
}

static inline void rows_2_bytes_core(uint16_t * dest, const uint16_t * above, const uint16_t * below, int32_t step,
                                     int32_t cnt, bool smooth, bool swapped)
{
    for(; cnt > 0; cnt--) {
        uint32_t px_above = above[0];
        uint32_t px_below = below[0];
        uint32_t px = dest[0];
        if(swapped) {
            px_above = lv_color_swap_16(px_above);
            px_below = lv_color_swap_16(px_below);
            px = lv_color_swap_16(px);
        }
        if(smooth) px = TENT(px_above, px, px_below, AVG_MASK_16);
        else px = AVG_FLOOR(px_above, px_below, AVG_MASK_16);
        if(swapped) px = lv_color_swap_16(px);
        dest[0] = px;
        dest += step;
        above += step;
        below += step;
    }
}

static void rows_3_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth)
{
    uint8_t * px = dest;
    const uint8_t * px_above = above;
    const uint8_t * px_below = below;
    for(; cnt > 0; cnt--) {
        if(smooth) {
            px[0] = TENT(px_above[0], px[0], px_below[0], AVG_MASK_8);
            px[1] = TENT(px_above[1], px[1], px_below[1], AVG_MASK_8);
            px[2] = TENT(px_above[2], px[2], px_below[2], AVG_MASK_8);
        }
        else {
            px[0] = (px_above[0] + px_below[0]) >> 1;
            px[1] = (px_above[1] + px_below[1]) >> 1;
            px[2] = (px_above[2] + px_below[2]) >> 1;
        }
        px += step;
        px_above += step;
        px_below += step;
    }
}

static void rows_4_bytes(void * dest, const void * above, const void * below, int32_t step, int32_t cnt,
                         bool smooth)
{
    uint32_t * px = dest;
    const uint32_t * px_above = above;
    const uint32_t * px_below = below;
    step /= 4;
    for(; cnt > 0; cnt--) {
        uint32_t res;
        if(smooth) res = TENT(px_above[0], px[0], px_below[0], AVG_MASK_8);
        else res = AVG_FLOOR(px_above[0], px_below[0], AVG_MASK_8);
        px[0] = (res & COLOR_MASK_32) | (px[0] & ~COLOR_MASK_32);
        px += step;
        px_above += step;
        px_below += step;
    }
}

#endif /*LV_USE_DRAW_SW*/
