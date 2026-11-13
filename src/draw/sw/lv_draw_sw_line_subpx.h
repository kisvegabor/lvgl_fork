/**
 * @file lv_draw_sw_line_subpx.h
 *
 */

#ifndef LV_DRAW_SW_LINE_SUBPX_H
#define LV_DRAW_SW_LINE_SUBPX_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "../lv_draw_private.h"

#if LV_USE_DRAW_SW

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Draw an anti-aliased line with sub-pixel endpoints, dashes and round caps.
 * Integer coordinates are pixel centers. Along the line it starts half a pixel before
 * `p1` and ends half a pixel before `p2`, and an even width is moved half a pixel up
 * and left, so a horizontal or vertical line on whole pixels has sharp edges.
 * @param layer       the layer to draw on
 * @param clip_area   draw only here
 * @param dsc         the line descriptor, p1 and p2 are used
 */
void lv_draw_sw_line_subpx(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_line_dsc_t * dsc);

#endif /*LV_USE_DRAW_SW*/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*LV_DRAW_SW_LINE_SUBPX_H*/
