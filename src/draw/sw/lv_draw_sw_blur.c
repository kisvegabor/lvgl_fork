/**
 * @file lv_draw_sw_blur.c
 *
 * The area is blurred in place in three steps:
 * 1. Halve the resolution 0..3 times. A level keeps every 2nd pixel of every 2nd line of the level before it.
 *    The kept pixels stay where they are in the buffer.
 * 2. Blur the pixels of the last level with an IIR filter in all 4 directions.
 * 3. Double the resolution back, level by level: the pixels in between are the average of their neighbors.
 *
 * The larger the blur radius, the lower the resolution can be without a visible difference.
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../misc/lv_area_private.h"
#include "lv_draw_sw_mask_private.h"
#include "../lv_draw_private.h"
#include "lv_draw_sw.h"
#include "lv_draw_sw_blur_private.h"

#if LV_USE_DRAW_SW

#include "../../core/lv_refr_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/** The rectangle to blur in the layer's buffer */
typedef struct {
    uint8_t * buf;      /**< The top left pixel */
    int32_t w;          /**< Width in pixels */
    int32_t h;          /**< Height in pixels */
    int32_t px_size;    /**< Size of a pixel in bytes */
    int32_t stride;     /**< Distance of the lines in bytes */
} blur_buf_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/

static int32_t get_levels(const lv_draw_blur_dsc_t * dsc, int32_t short_side);
static uint32_t get_intensity(uint32_t blur_radius, int32_t levels, uint32_t var_extra);
static uint32_t corners_copy(lv_layer_t * layer, const lv_area_t * coords, const lv_area_t * blur_area,
                             int32_t radius, uint8_t * save_buf, bool restore);
static void smooth(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format);
static void upsample(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format);
static void compact(blur_buf_t * b, const lv_draw_sw_blur_format_t * format, int32_t halvings);
static void expand(blur_buf_t * b, const lv_draw_sw_blur_format_t * format, int32_t w, int32_t h, bool duplicate);
static void rows_mix(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format, uint8_t * dest,
                     const uint8_t * above, const uint8_t * below, bool smooth);

static int32_t get_rounded_edge_point(int32_t p_start, int32_t p_end, int32_t p, int32_t r);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sw_blur(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_blur_dsc_t * dsc,
                     const lv_area_t * coords)
{
    if(dsc->blur_radius <= 0) return;

    const lv_draw_sw_blur_format_t * format = lv_draw_sw_blur_get_format(layer->draw_buf->header.cf);
    if(format == NULL) return;

    lv_area_t blur_area;
    if(!lv_area_intersect(&blur_area, coords, clip_area)) return;
    lv_area_move(&blur_area, -layer->buf_area.x1, -layer->buf_area.y1);

    LV_PROFILER_DRAW_BEGIN;

    blur_buf_t b;
    b.buf = lv_draw_buf_goto_xy(layer->draw_buf, blur_area.x1, blur_area.y1);
    b.w = lv_area_get_width(&blur_area);
    b.h = lv_area_get_height(&blur_area);
    b.px_size = lv_color_format_get_size(layer->draw_buf->header.cf);
    b.stride = layer->draw_buf->header.stride;

    int32_t levels = get_levels(dsc, LV_MIN(b.w, b.h));

    /*The first halvings are the slowest to smooth, and the levels after them hide most of the difference.
     *So with more levels the first one is not smoothed, and with the most levels and a large radius,
     *or if speed is preferred, the second one is not smoothed either.*/
    int32_t smooth_from = levels > 1 ? 1 : 0;
    if(levels == BLUR_LEVEL_MAX && (dsc->quality == LV_BLUR_QUALITY_SPEED || dsc->blur_radius >= 48)) smooth_from = 2;

    /*With the most levels the image is so smooth that the last, slowest doubling can copy the pixels*/
    bool duplicate = levels == BLUR_LEVEL_MAX;

    /*How much the smoothing and the doubling blur on their own: their variance in 1/8 pixel units*/
    uint32_t var_extra = 0;
    int32_t level;
    for(level = 0; level < levels; level++) {
        uint32_t dist_sqr = 1 << (2 * level);
        if(level >= smooth_from) var_extra += 4 * dist_sqr;
        var_extra += (level == 0 && duplicate) ? 2 * dist_sqr : 4 * dist_sqr;
    }

    uint32_t intensity = get_intensity(dsc->blur_radius, levels, var_extra);
    int32_t sample_len = LV_MAX((dsc->blur_radius >> levels) / 2, 1);

    /*The whole rectangle is blurred. Save the pixels outside of the rounded corners to restore them at the end.*/
    int32_t radius = dsc->corner_radius;
    int32_t short_side = LV_MIN(lv_area_get_width(coords), lv_area_get_height(coords));
    if(radius > short_side >> 1) radius = short_side >> 1;

    uint8_t * corner_buf = NULL;
    uint32_t corner_size = corners_copy(layer, coords, &blur_area, radius, NULL, false);
    if(corner_size) {
        corner_buf = lv_malloc(corner_size);
        LV_ASSERT_MALLOC(corner_buf);
        if(corner_buf == NULL) {
            LV_PROFILER_DRAW_END;
            return;
        }
        corners_copy(layer, coords, &blur_area, radius, corner_buf, false);
    }

    /*Halve the resolution.
     *After the last halving the kept pixels stay in place, `dist` pixels from each other.
     *After the others they are moved next to each other, so that the next steps work on neighboring pixels.
     *Halvings without smoothing in between are moved in one go.*/
    int32_t w_ori[BLUR_LEVEL_MAX];
    int32_t h_ori[BLUR_LEVEL_MAX];
    int32_t halvings = 0;
    for(level = 0; level < levels; level++) {
        if(level >= smooth_from) smooth(&b, format);
        if(level == levels - 1) break;

        w_ori[level] = ((b.w - 1) >> halvings) + 1;
        h_ori[level] = ((b.h - 1) >> halvings) + 1;
        halvings++;
        if(level + 1 >= smooth_from) {
            compact(&b, format, halvings);
            halvings = 0;
        }
    }

    int32_t dist = levels ? 2 : 1;
    if(intensity) {
        int32_t x;
        int32_t y;

        /*Blur each column top to bottom and bottom to top.*/
        int32_t cnt = (b.h - 1) / dist + 1;
        int32_t step = b.stride * dist;
        uint32_t sample_len_limited = LV_MIN(cnt, sample_len);
        for(x = 0; x < b.w; x += dist) {
            uint8_t * buf = b.buf + x * b.px_size;
            format->blur_cb(buf, step, cnt, sample_len_limited, intensity);
            format->blur_cb(buf + (cnt - 1) * step, -step, cnt, sample_len_limited, intensity);
        }

        /*Blur each line from left to right and right to left.*/
        cnt = (b.w - 1) / dist + 1;
        step = b.px_size * dist;
        sample_len_limited = LV_MIN(cnt, sample_len);
        for(y = 0; y < b.h; y += dist) {
            uint8_t * buf = b.buf + y * b.stride;
            format->blur_cb(buf, step, cnt, sample_len_limited, intensity);
            format->blur_cb(buf + (cnt - 1) * step, -step, cnt, sample_len_limited, intensity);
        }
    }

    /*Double the resolution back*/
    if(levels) upsample(&b, format);
    for(level = levels - 2; level >= 0; level--) {
        if(level == 0 && duplicate) {
            expand(&b, format, w_ori[level], h_ori[level], true);
        }
        else {
            expand(&b, format, w_ori[level], h_ori[level], false);
            upsample(&b, format);
        }
    }

    if(corner_buf) {
        corners_copy(layer, coords, &blur_area, radius, corner_buf, true);
        lv_free(corner_buf);
    }

    LV_PROFILER_DRAW_END;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Get how many times the resolution can be halved.
 * @param dsc           the blur descriptor
 * @param short_side    the shorter side of the area to blur
 * @return              0..BLUR_LEVEL_MAX
 */
static int32_t get_levels(const lv_draw_blur_dsc_t * dsc, int32_t short_side)
{
    int32_t levels;
    if(dsc->quality == LV_BLUR_QUALITY_PRECISION) {
        levels = 0;
    }
    else if(dsc->quality == LV_BLUR_QUALITY_SPEED) {
        if(dsc->blur_radius >= 12) levels = 3;
        else if(dsc->blur_radius >= 6) levels = 2;
        else levels = 1;
    }
    else {
        if(dsc->blur_radius >= 24) levels = 3;
        else if(dsc->blur_radius >= 8) levels = 2;
        else if(dsc->blur_radius >= 4) levels = 1;
        else levels = 0;
    }

    /*Keep at least a few pixels on the last level*/
    while(levels > 0 && short_side < (4 << levels)) levels--;

    return levels;
}

/**
 * Get the weight of the previous pixels in the IIR filter.
 * It is set so that the same radius looks the same on every level.
 * @param blur_radius   the blur radius
 * @param levels        how many times the resolution is halved
 * @param var_extra     the variance of the smoothing and the doubling steps in 1/8 pixel units
 * @return              0..BLUR_INTENSITY_MAX - 1
 */
static uint32_t get_intensity(uint32_t blur_radius, int32_t levels, uint32_t var_extra)
{
    /*How much a filter blurs is measured by its variance.
     *For the IIR filter applied back and forth it is `2 * a / (1 - a)^2`, where `a` is the intensity as 0..1.
     *On full resolution `a = r / (r + 4)`, so the variance is `r * (r + 4) / 8`. This is the target.
     *`var_extra` is already there, the rest is left to the IIR filter.
     *`n / d` is its variance on the low resolution.*/
    blur_radius = LV_MIN(blur_radius, 1000);
    int32_t n = blur_radius * (blur_radius + 4) - var_extra;
    if(n <= 0) return 0;
    uint32_t d = 8 << (2 * levels);

    /*Solved for `a`: a = (n + d - sqrt(d * (2 * n + d))) / n. The square root is in 1/16 units.*/
    uint32_t sqrt_arg = d * (2 * n + d);
    uint32_t sqrt_res = sqrt_arg < (1 << 24) ? (uint32_t)lv_sqrt32(sqrt_arg << 8) : (uint32_t)lv_sqrt32(sqrt_arg) << 4;

    return ((BLUR_INTENSITY_MAX / 16) * (16 * (n + d) - sqrt_res)) / n;
}

/**
 * Save or restore the pixels of the blurred area that are outside of the rounded corners.
 * @param layer         the layer to blur
 * @param coords        the coordinates of the rounded rectangle
 * @param blur_area     the blurred area, relative to the layer's buffer
 * @param radius        the corner radius
 * @param save_buf      buffer for the pixels. If NULL, the pixels are only counted.
 * @param restore       true: copy from `save_buf` to the layer, false: copy from the layer to `save_buf`
 * @return              the size of the pixels in bytes
 */
static uint32_t corners_copy(lv_layer_t * layer, const lv_area_t * coords, const lv_area_t * blur_area,
                             int32_t radius, uint8_t * save_buf, bool restore)
{
    uint32_t px_size = lv_color_format_get_size(layer->draw_buf->header.cf);
    uint32_t size = 0;
    int32_t y;
    for(y = blur_area->y1; y <= blur_area->y2; y++) {
        int32_t cir_x = get_rounded_edge_point(coords->y1, coords->y2, layer->buf_area.y1 + y, radius);
        if(cir_x == 0) continue;

        /*The left corner first, then the right one*/
        uint32_t i;
        for(i = 0; i < 2; i++) {
            int32_t x_from = blur_area->x1;
            int32_t x_to = blur_area->x2;
            if(i == 0) x_to = LV_MIN(x_to, coords->x1 - layer->buf_area.x1 + cir_x - 1);
            else x_from = LV_MAX(x_from, coords->x2 - layer->buf_area.x1 - cir_x + 1);
            if(x_from > x_to) continue;

            uint32_t len = (x_to - x_from + 1) * px_size;
            if(save_buf) {
                uint8_t * px = lv_draw_buf_goto_xy(layer->draw_buf, x_from, y);
                if(restore) lv_memcpy(px, save_buf + size, len);
                else lv_memcpy(save_buf + size, px, len);
            }
            size += len;
        }
    }

    return size;
}

/**
 * Smooth every 2nd pixel of every 2nd line, the ones that the next level keeps.
 * This way the dropped pixels count too.
 * @param b             the buffer
 * @param format        the functions of the color format
 */
static void smooth(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format)
{
    int32_t y;
    for(y = 0; y < b->h; y += 2) {
        /*The first and the last line have one neighbor. It is used for both sides.*/
        uint8_t * row = b->buf + y * b->stride;
        const uint8_t * below = y + 1 < b->h ? row + b->stride : row - b->stride;
        const uint8_t * above = y > 0 ? row - b->stride : below;
        if(b->h > 1) rows_mix(b, format, row, above, below, true);
        format->smooth_cb(row, b->px_size, b->w);
    }
}

/**
 * Double the resolution: every 2nd pixel and every 2nd line become the average of their neighbors.
 * @param b             the buffer
 * @param format        the functions of the color format
 */
static void upsample(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format)
{
    int32_t y;
    for(y = 0; y < b->h; y += 2) {
        format->upsample_cb(b->buf + y * b->stride, b->px_size, b->w);
    }

    for(y = 1; y < b->h; y += 2) {
        /*The last line can be without a line below. It is a copy of the line above.*/
        uint8_t * row = b->buf + y * b->stride;
        const uint8_t * above = row - b->stride;
        const uint8_t * below = y + 1 < b->h ? row + b->stride : above;
        rows_mix(b, format, row, above, below, false);
    }
}

/**
 * Halve the size of the buffer once or more: move every 2nd (4th, ...) pixel of every 2nd (4th, ...) line
 * to the top left corner.
 * Only the blurred channels are moved, the alpha of every pixel stays.
 * @param b             the buffer. Its size is updated.
 * @param format        the functions of the color format
 * @param halvings      how many times to halve the size
 */
static void compact(blur_buf_t * b, const lv_draw_sw_blur_format_t * format, int32_t halvings)
{
    int32_t w = ((b->w - 1) >> halvings) + 1;
    int32_t h = ((b->h - 1) >> halvings) + 1;
    int32_t y;
    for(y = 0; y < h; y++) {
        lv_draw_sw_blur_px_copy(b->buf + y * b->stride, b->buf + (y << halvings) * b->stride, b->px_size,
                                b->px_size << halvings, w, format->color_size);
    }
    b->w = w;
    b->h = h;
}

/**
 * The opposite of `compact`: spread the pixels to every 2nd pixel of every 2nd line.
 * @param b             the buffer. Its size is updated.
 * @param format        the functions of the color format
 * @param w             the new width
 * @param h             the new height
 * @param duplicate     true: also copy every pixel to the 3 pixels that it left empty
 */
static void expand(blur_buf_t * b, const lv_draw_sw_blur_format_t * format, int32_t w, int32_t h, bool duplicate)
{
    /*Go backwards to read every pixel before it is overwritten*/
    int32_t y;
    for(y = b->h - 1; y >= 0; y--) {
        uint8_t * src_last = b->buf + y * b->stride + (b->w - 1) * b->px_size;
        uint8_t * dest = b->buf + y * 2 * b->stride;
        uint8_t * dest_last = dest + (b->w - 1) * 2 * b->px_size;
        if(!duplicate) {
            lv_draw_sw_blur_px_copy(dest_last, src_last, -b->px_size * 2, -b->px_size, b->w, format->color_size);
            continue;
        }

        /*With an odd width the last pixel has no pair*/
        if(w & 1) {
            lv_draw_sw_blur_px_copy(dest_last, src_last, 0, 0, 1, format->color_size);
            dest_last -= b->px_size * 2;
            src_last -= b->px_size;
        }
        lv_draw_sw_blur_px_double(dest_last, src_last, b->px_size, w / 2, format->color_size);

        if(y * 2 + 1 < h) {
            lv_draw_sw_blur_px_copy(dest + b->stride, dest, b->px_size, b->px_size, w, format->color_size);
        }
    }
    b->w = w;
    b->h = h;
}

/**
 * Mix the pixels of a line with the lines above and below it. See `lv_draw_sw_blur_rows_cb_t`.
 * 4 bytes are mixed at once if the lines are aligned the same way.
 */
static void rows_mix(const blur_buf_t * b, const lv_draw_sw_blur_format_t * format, uint8_t * dest,
                     const uint8_t * above, const uint8_t * below, bool smooth)
{
    int32_t step = b->px_size;
    int32_t cnt = b->w;
    bool same_align = (((lv_uintptr_t)dest ^ (lv_uintptr_t)above) & 0x3) == 0 &&
                      (((lv_uintptr_t)dest ^ (lv_uintptr_t)below) & 0x3) == 0;
    if(!same_align) {
        format->rows_cb(dest, above, below, step, cnt, smooth);
        return;
    }

    /*Mix pixel by pixel until a 4 byte boundary*/
    int32_t head_cnt = 0;
    while(((lv_uintptr_t)(dest + head_cnt * step) & 0x3) && head_cnt < cnt) head_cnt++;
    format->rows_cb(dest, above, below, step, head_cnt, smooth);
    dest += head_cnt * step;
    above += head_cnt * step;
    below += head_cnt * step;
    cnt -= head_cnt;

    /*4 bytes at once, ending on a pixel boundary: 3 byte pixels need groups of 12 bytes*/
    int32_t word_cnt = b->px_size == 3 ? (cnt / 4) * 3 : (cnt * b->px_size) / 4;
    uint32_t * dest32 = (uint32_t *)dest;
    const uint32_t * above32 = (const uint32_t *)above;
    const uint32_t * below32 = (const uint32_t *)below;
    uint32_t avg_mask = format->avg_mask;
    uint32_t keep_mask = format->keep_mask;
    int32_t i;
    if(format->swapped) {
        for(i = 0; i < word_cnt; i++) {
            uint32_t px_above = SWAP_16X2(above32[i]);
            uint32_t px_below = SWAP_16X2(below32[i]);
            uint32_t res;
            if(smooth) {
                uint32_t px = SWAP_16X2(dest32[i]);
                res = TENT(px_above, px, px_below, avg_mask);
            }
            else {
                res = AVG_FLOOR(px_above, px_below, avg_mask);
            }
            dest32[i] = SWAP_16X2(res);
        }
    }
    else if(smooth) {
        for(i = 0; i < word_cnt; i++) {
            uint32_t px = dest32[i];
            uint32_t res = TENT(above32[i], px, below32[i], avg_mask);
            dest32[i] = (res & ~keep_mask) | (px & keep_mask);
        }
    }
    else if(keep_mask) {
        for(i = 0; i < word_cnt; i++) {
            dest32[i] = (AVG_FLOOR(above32[i], below32[i], avg_mask) & ~keep_mask) | (dest32[i] & keep_mask);
        }
    }
    else {
        for(i = 0; i < word_cnt; i++) {
            dest32[i] = AVG_FLOOR(above32[i], below32[i], avg_mask);
        }
    }

    /*The rest pixel by pixel*/
    int32_t done_cnt = (word_cnt * 4) / b->px_size;
    format->rows_cb(dest + done_cnt * step, above + done_cnt * step, below + done_cnt * step, step,
                    cnt - done_cnt, smooth);
}

/**
 * Get the X or Y point for a rounded edge.
 * If the X coordinates are used Y will be returned and vice versa
 * Calculates the left or top edge
 * @param p_start   the edge's X1 or Y1 coordinate
 * @param p_end     the edge's X2 or Y2 coordinate
 * @param p         the X or Y coordinate on the edge for which the related X or X should be returned
 * @param r         the radius of the corner
 * @return          the X or Y coordinate corresponding to the provided coordinates
 */
static int32_t get_rounded_edge_point(int32_t p_start, int32_t p_end, int32_t p, int32_t r)
{
    if(p < p_start + r) p = r - (p - p_start);
    else if(p > p_end - r) p = r - (p_end - p);
    else return 0;

    uint32_t res = lv_sqrt32(r * r - p * p);
    return r - res;
}


#endif /*LV_USE_DRAW_SW*/
