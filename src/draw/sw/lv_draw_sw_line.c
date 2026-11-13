/**
 * @file lv_draw_sw_line.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../misc/lv_area_private.h"
#include "blend/lv_draw_sw_blend_private.h"
#include "../lv_draw_private.h"
#include "lv_draw_sw.h"
#include "lv_draw_sw_line_subpx.h"

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
static bool is_int(const lv_draw_line_dsc_t * dsc);
static void /* LV_ATTRIBUTE_FAST_MEM */ draw_line_fill(lv_layer_t * layer, const lv_area_t * clip_area,
                                                       const lv_draw_line_dsc_t * dsc);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sw_line(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_line_dsc_t * dsc, void * user_data)
{
    LV_UNUSED(user_data);

    if(dsc->width == 0) return;
    if(dsc->opa <= LV_OPA_MIN) return;

    if(dsc->p1.x == dsc->p2.x && dsc->p1.y == dsc->p2.y) return;

    LV_PROFILER_DRAW_BEGIN;
    /*A solid horizontal or vertical line on whole pixels is a rectangle.
     *lv_draw_sw_line_subpx() would give the same pixels, just slower.*/
    bool dashed = dsc->dash_gap && dsc->dash_width;
    bool round = dsc->round_start || dsc->round_end;
    bool hor_ver = dsc->p1.x == dsc->p2.x || dsc->p1.y == dsc->p2.y;
    if(hor_ver && !dashed && !round && is_int(dsc)) draw_line_fill(layer, clip_area, dsc);
    else lv_draw_sw_line_subpx(layer, clip_area, dsc);
    LV_PROFILER_DRAW_END;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static bool is_int(const lv_draw_line_dsc_t * dsc)
{
#if LV_USE_FLOAT
    return dsc->p1.x == (int32_t)dsc->p1.x && dsc->p1.y == (int32_t)dsc->p1.y &&
           dsc->p2.x == (int32_t)dsc->p2.x && dsc->p2.y == (int32_t)dsc->p2.y;
#else
    LV_UNUSED(dsc);
    return true;
#endif
}

static void LV_ATTRIBUTE_FAST_MEM draw_line_fill(lv_layer_t * layer, const lv_area_t * clip_area,
                                                 const lv_draw_line_dsc_t * dsc)
{
    int32_t w = dsc->width - 1;
    int32_t w_half0 = w >> 1;
    int32_t w_half1 = w_half0 + (w & 0x1); /*Compensate rounding error*/

    lv_area_t blend_area;
    if(dsc->p1.y == dsc->p2.y) {
        blend_area.x1 = (int32_t)LV_MIN(dsc->p1.x, dsc->p2.x);
        blend_area.x2 = (int32_t)LV_MAX(dsc->p1.x, dsc->p2.x) - 1;
        blend_area.y1 = (int32_t)dsc->p1.y - w_half1;
        blend_area.y2 = (int32_t)dsc->p1.y + w_half0;
    }
    else {
        blend_area.x1 = (int32_t)dsc->p1.x - w_half1;
        blend_area.x2 = (int32_t)dsc->p1.x + w_half0;
        blend_area.y1 = (int32_t)LV_MIN(dsc->p1.y, dsc->p2.y);
        blend_area.y2 = (int32_t)LV_MAX(dsc->p1.y, dsc->p2.y) - 1;
    }

    if(!lv_area_intersect(&blend_area, &blend_area, clip_area)) return;

    lv_draw_sw_blend_dsc_t blend_dsc;
    lv_memzero(&blend_dsc, sizeof(blend_dsc));
    blend_dsc.blend_area = &blend_area;
    blend_dsc.color = dsc->color;
    blend_dsc.opa = dsc->opa;
    lv_draw_sw_blend(layer, clip_area, &blend_dsc);
}

#endif /*LV_USE_DRAW_SW*/
