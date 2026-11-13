/**
 * @file lv_draw_sw_blur_private.h
 *
 */

#ifndef LV_DRAW_SW_BLUR_PRIVATE_H
#define LV_DRAW_SW_BLUR_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lv_draw_sw.h"

#if LV_USE_DRAW_SW

/*********************
 *      DEFINES
 *********************/
#define BLUR_INTENSITY_BITS 12
#define BLUR_INTENSITY_MAX (1 << 12)
#define BLUR_INTENSITY_HALF ((1 << 12) / 2)

#define BLUR_LEVEL_MAX 3

/*The color channels of an ARGB8888 pixel read as `uint32_t`*/
#if LV_BIG_ENDIAN_SYSTEM
#define COLOR_MASK_32 0xFFFFFF00
#else
#define COLOR_MASK_32 0x00FFFFFF
#endif

/*The alpha channel of 2 AL88 pixels read as `uint32_t`*/
#if LV_BIG_ENDIAN_SYSTEM
#define ALPHA_MASK_AL88 0x00FF00FF
#else
#define ALPHA_MASK_AL88 0xFF00FF00
#endif

/*The average of the channels of 2 pixels, or of more pixels packed in one variable, rounded down or up.
 *`mask` has a 0 bit on the lowest bit of each channel.*/
#define AVG_FLOOR(a, b, mask) (((a) & (b)) + ((((a) ^ (b)) & (mask)) >> 1))
#define AVG_CEIL(a, b, mask) (((a) | (b)) - ((((a) ^ (b)) & (mask)) >> 1))

/*(prev + 2 * px + next) / 4 for each channel. Rounding once up and once down keeps the brightness.*/
#define TENT(prev, px, next, mask) AVG_FLOOR(AVG_CEIL(prev, next, mask), px, mask)

#define AVG_MASK_8  0xFEFEFEFE
#define AVG_MASK_16 0xF7DEF7DE

/*Swap the bytes of the 2 RGB565 pixels in a `uint32_t`*/
#define SWAP_16X2(v) ((((v) & 0x00FF00FF) << 8) | (((v) >> 8) & 0x00FF00FF))

/**********************
 *      TYPEDEFS
 **********************/

/**
 * Blur `cnt` pixels of a line or a column in one direction.
 *
 * Every format uses the same filter on each color channel:
 * `sum = sum * intensity + (px + 0.5) * (1 - intensity)`, and the new pixel is the integer part of `sum`.
 * The 0.5 makes it round to the nearest value.
 * A pixel that already equals the integer part of `sum` is left as it is, and `sum` is not updated.
 * @param buf           the first pixel to blur
 * @param step          the distance of the next pixel in bytes. Negative to go backwards.
 * @param cnt           the number of pixels to blur
 * @param sample_len    the filter starts from the average of this many pixels
 * @param intensity     the weight of the previous pixels, 0..BLUR_INTENSITY_MAX - 1
 */
typedef void (*lv_draw_sw_blur_line_cb_t)(void * buf, int32_t step, int32_t cnt, uint32_t sample_len,
                                          uint32_t intensity);

/**
 * Process `cnt` pixels of a line. Used with `smooth_...` and `upsample_...`:
 * - smooth: every 2nd pixel, starting with the first, becomes (previous + 2 * itself + next) / 4.
 *   It is done before the other pixels are dropped.
 * - upsample: every 2nd pixel, starting with the second, becomes (previous + next) / 2.
 * @param buf           the first pixel
 * @param step          the distance of the next pixel in bytes
 * @param cnt           the number of pixels
 */
typedef void (*lv_draw_sw_blur_px_cb_t)(void * buf, int32_t step, int32_t cnt);

/**
 * Mix `cnt` pixels of a line with the lines above and below it.
 * @param dest          the first pixel of the line to write
 * @param above         the first pixel of the line above
 * @param below         the first pixel of the line below
 * @param step          the distance of the next pixel in bytes
 * @param cnt           the number of pixels
 * @param smooth        true: dest = (above + 2 * dest + below) / 4, false: dest = (above + below) / 2
 */
typedef void (*lv_draw_sw_blur_rows_cb_t)(void * dest, const void * above, const void * below, int32_t step,
                                          int32_t cnt, bool smooth);

/** The functions and constants to blur a color format */
typedef struct {
    lv_draw_sw_blur_line_cb_t blur_cb;
    lv_draw_sw_blur_px_cb_t smooth_cb;
    lv_draw_sw_blur_px_cb_t upsample_cb;
    lv_draw_sw_blur_rows_cb_t rows_cb;
    uint32_t avg_mask;      /**< `mask` of `AVG_FLOOR` for 4 bytes of pixels */
    uint32_t keep_mask;     /**< The bits of 4 bytes of pixels that are not blurred (alpha) */
    bool swapped;           /**< The 2 bytes of each pixel are swapped */
    uint8_t color_size;     /**< The first bytes of a pixel that are blurred. The rest is alpha. */
} lv_draw_sw_blur_format_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Get the functions that can blur a color format.
 * The alpha channel is not blurred: it stays as it is in every pixel.
 * @param cf    a color format
 * @return      the functions, or NULL if the format cannot be blurred, e.g. it is indexed
 */
const lv_draw_sw_blur_format_t * lv_draw_sw_blur_get_format(lv_color_format_t cf);

/**
 * Copy the first bytes of some pixels.
 * @param dest          the first pixel to write
 * @param src           the first pixel to read
 * @param dest_step     the distance of the next pixel to write in bytes. Can be negative.
 * @param src_step      the distance of the next pixel to read in bytes. Can be negative.
 * @param cnt           the number of pixels to copy
 * @param size          the number of bytes to copy from each pixel, 1..4
 */
void lv_draw_sw_blur_px_copy(uint8_t * dest, const uint8_t * src, int32_t dest_step, int32_t src_step, int32_t cnt,
                             uint32_t size);

/**
 * Write every pixel of a line twice: to 2 neighboring pixels of a line that is 2 times longer.
 * It goes backwards, so the 2 lines can start at the same address.
 * @param dest_last     the first one of the last 2 pixels to write
 * @param src_last      the last pixel to read
 * @param px_size       the size of a pixel in bytes
 * @param cnt           the number of pixels to read
 * @param size          the number of bytes to copy from each pixel, 1..4
 */
void lv_draw_sw_blur_px_double(uint8_t * dest_last, const uint8_t * src_last, int32_t px_size, int32_t cnt,
                               uint32_t size);

/**********************
 *      MACROS
 **********************/

#endif /*LV_USE_DRAW_SW*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DRAW_SW_BLUR_PRIVATE_H*/
