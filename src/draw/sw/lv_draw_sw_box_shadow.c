/**
 * @file lv_draw_sw_box_shadow.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../misc/lv_area_private.h"
#include "lv_draw_sw_mask_private.h"
#include "../lv_draw_private.h"
#include "lv_draw_sw.h"
#if LV_USE_DRAW_SW

#if LV_DRAW_SW_COMPLEX

#include "blend/lv_draw_sw_blend_private.h"

/*********************
 *      DEFINES
 *********************/
/*The sum of the blur filter is turned into an opacity with a multiplication and this shift*/
#define SHADOW_NORM_SHIFT   24

/**********************
 *      TYPEDEFS
 **********************/

/**
 * The shadow is drawn in 9 parts. The type tells where the opacity of a part comes from.
 */
typedef enum {
    SHADOW_PART_CORNER,     /**< A block of the corner buffer*/
    SHADOW_PART_VER_SIDE,   /**< Left or right side: the last row of the corner buffer on every line*/
    SHADOW_PART_HOR_SIDE,   /**< Top or bottom side: one value per line, from the first column of the corner buffer*/
    SHADOW_PART_CENTER,     /**< Fully covered*/
} shadow_part_type_t;

/**
 * How the lines of a part relate to the background rectangle.
 */
typedef enum {
    SHADOW_ROWS_FREE,       /**< The background doesn't affect these lines*/
    SHADOW_ROWS_BESIDE_BG,  /**< On the lines of the straight edges of the background, next to it*/
    SHADOW_ROWS_MASKED,     /**< On the lines of the rounded corners of the background*/
} shadow_rows_t;

typedef struct {
    lv_layer_t * layer;
    const lv_area_t * clip_area;
    const lv_area_t * bg_area;
    int32_t r_bg;
    void ** masks;
    bool simple;                    /**< The background covers the shadow below it, so no masking is needed*/
    lv_opa_t opa;                   /**< Opacity of the shadow, `LV_OPA_COVER` if close to it*/
    const lv_opa_t * sh_buf;        /**< The corner buffer in the orientation of the part being drawn*/
    int32_t corner_size;
    int32_t src_x;                  /**< X coordinate of the first column of `sh_buf` on the layer*/
    int32_t src_y;                  /**< Y coordinate of the first row of `sh_buf` on the layer*/
    lv_opa_t * mask_buf;            /**< One line to build a mask in*/
    lv_draw_sw_blend_dsc_t blend_dsc;
    lv_area_t blend_area;
    lv_area_t mask_area;
} shadow_draw_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void draw_part(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * part_area);
static void draw_bg_band(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * band, int32_t r);
static void draw_rows(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * area, shadow_rows_t rows);
static void draw_rows_masked(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * area);
static void blend_value(shadow_draw_t * d, lv_opa_t value, const lv_area_t * area);
static void corner_buf_mirror_hor(lv_opa_t * sh_buf, int32_t size);
static void corner_buf_flip_ver(lv_opa_t * sh_buf, int32_t size);
static bool /* LV_ATTRIBUTE_FAST_MEM */ corner_buf_calc(lv_opa_t * buf, int32_t sw, int32_t r, int32_t core_w,
                                                        int32_t core_h);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_sw_box_shadow(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_box_shadow_dsc_t * dsc,
                           const lv_area_t * coords)
{
    /*Calculate the rectangle which is blurred to get the shadow in `shadow_area`*/
    lv_area_t core_area;
    core_area.x1 = coords->x1  + dsc->ofs_x - dsc->spread;
    core_area.x2 = coords->x2  + dsc->ofs_x + dsc->spread;
    core_area.y1 = coords->y1  + dsc->ofs_y - dsc->spread;
    core_area.y2 = coords->y2  + dsc->ofs_y + dsc->spread;

    /*Calculate the bounding box of the shadow*/
    lv_area_t shadow_area;
    shadow_area.x1 = core_area.x1 - dsc->width / 2 - 1;
    shadow_area.x2 = core_area.x2 + dsc->width / 2 + 1;
    shadow_area.y1 = core_area.y1 - dsc->width / 2 - 1;
    shadow_area.y2 = core_area.y2 + dsc->width / 2 + 1;

    /*Get clipped draw area which is the real draw area.
     *It is always the same or inside `shadow_area`*/
    lv_area_t draw_area;
    if(!lv_area_intersect(&draw_area, &shadow_area, clip_area)) return;

    /*Consider 1 px smaller bg to be sure the edge will be covered by the shadow*/
    lv_area_t bg_area;
    bg_area = *coords;
    lv_area_increase(&bg_area, -1, -1);

    /*Get the clamped radius*/
    int32_t r_bg = dsc->radius;
    int32_t short_side = LV_MIN(lv_area_get_width(&bg_area), lv_area_get_height(&bg_area));
    if(r_bg > short_side >> 1) r_bg = short_side >> 1;

    /*A negative spread can make the core area empty. Then there is nothing to blur.*/
    int32_t core_w = lv_area_get_width(&core_area);
    int32_t core_h = lv_area_get_height(&core_area);
    if(core_w < 1 || core_h < 1) return;

    /*Get the clamped radius*/
    int32_t r_sh = dsc->radius;
    short_side = LV_MIN(core_w, core_h);
    if(r_sh > short_side >> 1) r_sh = short_side >> 1;

    /*Get how many pixels are affected by the blur on the corners*/
    int32_t corner_size = dsc->width  + r_sh;

    lv_opa_t * sh_buf = lv_malloc(corner_size * corner_size);
    LV_ASSERT_MALLOC(sh_buf);
    if(!sh_buf || !corner_buf_calc(sh_buf, dsc->width, r_sh, core_w, core_h)) {
        LV_LOG_WARN("Failed to allocate memory for shadow buffer");
        lv_free(sh_buf);
        return;
    }

    /*Skip a lot of masking if the background will cover the shadow that would be masked out*/
    bool simple = dsc->bg_cover;

    /*Create a radius mask to clip remove shadow on the bg area*/
    lv_draw_sw_mask_radius_param_t mask_rout_param;
    void * masks[2] = {0};
    if(!simple) {
        lv_draw_sw_mask_radius_init(&mask_rout_param, &bg_area, r_bg, true);
        masks[0] = &mask_rout_param;
    }

    lv_opa_t * mask_buf = lv_malloc(lv_area_get_width(&shadow_area));
    if(!mask_buf) {
        LV_LOG_WARN("Failed to allocate memory for shadow mask buffer");
        lv_free(sh_buf);
        if(!simple) {
            lv_draw_sw_mask_free_param(&mask_rout_param);
        }
        return;
    }

    shadow_draw_t d;
    d.layer = layer;
    d.clip_area = clip_area;
    d.bg_area = &bg_area;
    d.r_bg = r_bg;
    d.masks = masks;
    d.simple = simple;
    d.opa = dsc->opa >= LV_OPA_MAX ? LV_OPA_COVER : dsc->opa;
    d.sh_buf = sh_buf;
    d.corner_size = corner_size;
    d.mask_buf = mask_buf;
    lv_memzero(&d.blend_dsc, sizeof(d.blend_dsc));
    d.blend_dsc.blend_area = &d.blend_area;
    d.blend_dsc.mask_area = &d.mask_area;
    d.blend_dsc.color = dsc->color;

    /*The parts meet at these lines, so they never overlap even if the corners would*/
    int32_t w_half = shadow_area.x1 + lv_area_get_width(&shadow_area) / 2;
    int32_t h_half = shadow_area.y1 + lv_area_get_height(&shadow_area) / 2;

    int32_t left_x2 = LV_MIN(shadow_area.x1 + corner_size - 1, w_half - 1);
    int32_t right_x1 = LV_MAX(shadow_area.x2 - corner_size + 1, w_half);
    int32_t top_y2 = LV_MIN(shadow_area.y1 + corner_size - 1, h_half);
    int32_t bottom_y1 = LV_MAX(shadow_area.y2 - corner_size + 1, h_half + 1);
    int32_t mid_x1 = shadow_area.x1 + corner_size;
    int32_t mid_x2 = shadow_area.x2 - corner_size;
    int32_t mid_y1 = LV_MIN(shadow_area.y1 + corner_size, h_half + 1);
    int32_t mid_y2 = LV_MAX(shadow_area.y2 - corner_size, h_half);

    lv_area_t part_area;

    /*`sh_buf` holds the top right corner. It is mirrored and flipped to get the other corners.*/
    d.src_x = shadow_area.x2 - corner_size + 1;
    d.src_y = shadow_area.y1;

    /*Top right corner*/
    lv_area_set(&part_area, right_x1, shadow_area.y1, shadow_area.x2, top_y2);
    draw_part(&d, SHADOW_PART_CORNER, &part_area);

    /*Top side*/
    lv_area_set(&part_area, mid_x1, shadow_area.y1, mid_x2, top_y2);
    draw_part(&d, SHADOW_PART_HOR_SIDE, &part_area);

    /*Right side*/
    lv_area_set(&part_area, right_x1, mid_y1, shadow_area.x2, mid_y2);
    draw_part(&d, SHADOW_PART_VER_SIDE, &part_area);

    /*Center*/
    lv_area_set(&part_area, mid_x1, mid_y1, mid_x2, mid_y2);
    draw_part(&d, SHADOW_PART_CENTER, &part_area);

    bool has_bottom = draw_area.y2 > h_half;
    bool has_left = draw_area.x1 < w_half;

    if(has_bottom) {
        corner_buf_flip_ver(sh_buf, corner_size);
        d.src_y = shadow_area.y2 - corner_size + 1;

        /*Bottom right corner*/
        lv_area_set(&part_area, right_x1, bottom_y1, shadow_area.x2, shadow_area.y2);
        draw_part(&d, SHADOW_PART_CORNER, &part_area);

        /*Bottom side*/
        lv_area_set(&part_area, mid_x1, bottom_y1, mid_x2, shadow_area.y2);
        draw_part(&d, SHADOW_PART_HOR_SIDE, &part_area);
    }

    if(has_left) {
        corner_buf_mirror_hor(sh_buf, corner_size);
        d.src_x = shadow_area.x1;

        if(has_bottom) {
            /*Bottom left corner*/
            lv_area_set(&part_area, shadow_area.x1, bottom_y1, left_x2, shadow_area.y2);
            draw_part(&d, SHADOW_PART_CORNER, &part_area);

            corner_buf_flip_ver(sh_buf, corner_size);
        }
        d.src_y = shadow_area.y1;

        /*Top left corner*/
        lv_area_set(&part_area, shadow_area.x1, shadow_area.y1, left_x2, top_y2);
        draw_part(&d, SHADOW_PART_CORNER, &part_area);

        /*Left side*/
        lv_area_set(&part_area, shadow_area.x1, mid_y1, left_x2, mid_y2);
        draw_part(&d, SHADOW_PART_VER_SIDE, &part_area);
    }

    if(!simple) {
        lv_draw_sw_mask_free_param(&mask_rout_param);
    }
    lv_free(sh_buf);
    lv_free(mask_buf);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Draw one of the 9 parts of the shadow.
 * The part is cut to horizontal bands by the background rectangle, because only the
 * rounded corners of the background need a mask.
 * @param d         the draw context
 * @param type      tells where the opacity of the part comes from
 * @param part_area the area of the part on the layer
 */
static void draw_part(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * part_area)
{
    lv_area_t a;
    if(!lv_area_intersect(&a, part_area, d->clip_area)) return;
    if(lv_area_is_in(&a, d->bg_area, d->r_bg)) return;

    const lv_area_t * bg = d->bg_area;
    if(bg->y1 > bg->y2) {
        draw_rows(d, type, &a, SHADOW_ROWS_FREE);
        return;
    }

    int32_t r = LV_MAX(d->r_bg, 0);
    lv_area_t band = a;

    /*Above the background*/
    band.y2 = LV_MIN(a.y2, bg->y1 - 1);
    draw_rows(d, type, &band, SHADOW_ROWS_FREE);

    /*Top rounded corners of the background*/
    band.y1 = LV_MAX(a.y1, bg->y1);
    band.y2 = LV_MIN(a.y2, bg->y1 + r - 1);
    draw_bg_band(d, type, &band, r);

    /*Straight edges of the background*/
    band.y1 = LV_MAX(a.y1, bg->y1 + r);
    band.y2 = LV_MIN(a.y2, bg->y2 - r);
    draw_bg_band(d, type, &band, 0);

    /*Bottom rounded corners of the background*/
    band.y1 = LV_MAX(a.y1, bg->y2 - r + 1);
    band.y2 = LV_MIN(a.y2, bg->y2);
    draw_bg_band(d, type, &band, r);

    /*Below the background*/
    band.y1 = LV_MAX(a.y1, bg->y2 + 1);
    band.y2 = a.y2;
    draw_rows(d, type, &band, SHADOW_ROWS_FREE);
}

/**
 * Draw the lines of a part which cross the background.
 * The pixels fully under the background are not drawn.
 * @param d     the draw context
 * @param type  type of the part
 * @param band  the area to draw. Can be empty.
 * @param r     width of the rounded corners of the background on these lines.
 *              0 on the lines of the straight edges.
 */
static void draw_bg_band(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * band, int32_t r)
{
    const lv_area_t * bg = d->bg_area;
    if(bg->x1 > bg->x2) {
        draw_rows(d, type, band, SHADOW_ROWS_BESIDE_BG);
        return;
    }

    /*The anti-aliasing of the radius mask can reach one pixel out of the background*/
    int32_t aa = r > 0 ? 1 : 0;
    lv_area_t piece = *band;

    /*On the left of the background*/
    piece.x2 = LV_MIN(band->x2, bg->x1 - 1 - aa);
    draw_rows(d, type, &piece, SHADOW_ROWS_BESIDE_BG);

    /*On the right of the background*/
    piece.x1 = LV_MAX(band->x1, bg->x2 + 1 + aa);
    piece.x2 = band->x2;
    draw_rows(d, type, &piece, SHADOW_ROWS_BESIDE_BG);

    if(r > 0) {
        shadow_rows_t round_rows = d->simple ? SHADOW_ROWS_FREE : SHADOW_ROWS_MASKED;

        /*Left rounded corner of the background*/
        piece.x1 = LV_MAX(band->x1, bg->x1 - aa);
        piece.x2 = LV_MIN(band->x2, bg->x1 + r - 1);
        draw_rows(d, type, &piece, round_rows);

        /*Right rounded corner of the background*/
        piece.x1 = LV_MAX(band->x1, bg->x2 - r + 1);
        piece.x2 = LV_MIN(band->x2, bg->x2 + aa);
        draw_rows(d, type, &piece, round_rows);
    }
}

/**
 * Draw an area of a part where every line is handled the same way
 * @param d     the draw context
 * @param type  type of the part
 * @param area  the area to draw. Can be empty.
 * @param rows  tells how the background affects these lines
 */
static void draw_rows(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * area, shadow_rows_t rows)
{
    if(area->x1 > area->x2 || area->y1 > area->y2) return;

    if(rows == SHADOW_ROWS_MASKED) {
        draw_rows_masked(d, type, area);
        return;
    }

    lv_draw_sw_blend_dsc_t * blend_dsc = &d->blend_dsc;
    int32_t corner_size = d->corner_size;
    int32_t y;

    d->blend_area = *area;
    blend_dsc->opa = d->opa;
    blend_dsc->mask_res = LV_DRAW_SW_MASK_RES_CHANGED;

    switch(type) {
        case SHADOW_PART_CORNER:
            /*The corner buffer is the mask of the whole block*/
            blend_dsc->mask_buf = d->sh_buf;
            lv_area_set(&d->mask_area, d->src_x, d->src_y, d->src_x + corner_size - 1, d->src_y + corner_size - 1);
            lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
            break;
        case SHADOW_PART_VER_SIDE:
            blend_dsc->mask_buf = d->sh_buf + (corner_size - 1) * corner_size;
            d->mask_area.x1 = d->src_x;
            d->mask_area.x2 = d->src_x + corner_size - 1;
            for(y = area->y1; y <= area->y2; y++) {
                d->blend_area.y1 = y;
                d->blend_area.y2 = y;
                d->mask_area.y1 = y;
                d->mask_area.y2 = y;
                lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
            }
            break;
        case SHADOW_PART_HOR_SIDE:
            for(y = area->y1; y <= area->y2; y++) {
                d->blend_area.y1 = y;
                d->blend_area.y2 = y;
                blend_value(d, d->sh_buf[(y - d->src_y) * corner_size], &d->blend_area);
            }
            break;
        case SHADOW_PART_CENTER:
            /*With a mask the blending mixes the mask value with the opacity. Do the same here
             *to get the same color as on the masked lines.*/
            if(rows == SHADOW_ROWS_BESIDE_BG && !d->simple) {
                blend_value(d, LV_OPA_COVER, area);
            }
            else {
                blend_dsc->mask_buf = NULL;
                lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
            }
            break;
    }
}

/**
 * Draw an area of a part on the lines of the rounded corners of the background.
 * The mask of the background is applied line by line.
 * @param d     the draw context
 * @param type  type of the part
 * @param area  the area to draw
 */
static void draw_rows_masked(shadow_draw_t * d, shadow_part_type_t type, const lv_area_t * area)
{
    lv_draw_sw_blend_dsc_t * blend_dsc = &d->blend_dsc;
    int32_t corner_size = d->corner_size;
    int32_t w = lv_area_get_width(area);
    const lv_opa_t * src = d->sh_buf + (area->x1 - d->src_x);
    if(type == SHADOW_PART_VER_SIDE) src += (corner_size - 1) * corner_size;

    d->blend_area = *area;
    d->mask_area = *area;
    blend_dsc->opa = d->opa;
    blend_dsc->mask_buf = d->mask_buf;

    int32_t y;
    for(y = area->y1; y <= area->y2; y++) {
        d->blend_area.y1 = y;
        d->blend_area.y2 = y;
        d->mask_area.y1 = y;
        d->mask_area.y2 = y;

        switch(type) {
            case SHADOW_PART_CORNER:
                lv_memcpy(d->mask_buf, src + (y - d->src_y) * corner_size, w);
                break;
            case SHADOW_PART_VER_SIDE:
                lv_memcpy(d->mask_buf, src, w);
                break;
            case SHADOW_PART_HOR_SIDE:
                lv_memset(d->mask_buf, d->sh_buf[(y - d->src_y) * corner_size], w);
                break;
            case SHADOW_PART_CENTER:
                lv_memset(d->mask_buf, 0xff, w);
                break;
        }

        blend_dsc->mask_res = lv_draw_sw_mask_apply(d->masks, d->mask_buf, area->x1, y, w);
        /*Except the center, the mask buffer needs to be used even if the mask didn't change it*/
        if(blend_dsc->mask_res == LV_DRAW_SW_MASK_RES_FULL_COVER && type != SHADOW_PART_CENTER) {
            blend_dsc->mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
        }
        lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
    }
}

/**
 * Draw an area where every pixel has the same shadow value
 * @param d     the draw context
 * @param value the value from the corner buffer
 * @param area  the area to draw
 */
static void blend_value(shadow_draw_t * d, lv_opa_t value, const lv_area_t * area)
{
    lv_draw_sw_blend_dsc_t * blend_dsc = &d->blend_dsc;
    lv_opa_t opa = d->opa == LV_OPA_COVER ? value : LV_OPA_MIX2(value, d->opa);

    d->blend_area = *area;

    /*Blending with an opacity is faster than with a mask.
     *Very low and very high opacities are rounded to transparent and opaque, so without
     *`simple` they need the mask to get the same color as on the masked lines.*/
    if(d->simple || (opa > LV_OPA_MIN && opa < LV_OPA_MAX)) {
        blend_dsc->opa = opa;
        blend_dsc->mask_buf = NULL;
        lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
        return;
    }

    lv_memset(d->mask_buf, value, lv_area_get_width(area));
    blend_dsc->opa = d->opa;
    blend_dsc->mask_buf = d->mask_buf;
    blend_dsc->mask_res = LV_DRAW_SW_MASK_RES_CHANGED;
    d->mask_area = *area;

    int32_t y;
    for(y = area->y1; y <= area->y2; y++) {
        d->blend_area.y1 = y;
        d->blend_area.y2 = y;
        d->mask_area.y1 = y;
        d->mask_area.y2 = y;
        lv_draw_sw_blend(d->layer, d->clip_area, blend_dsc);
    }
}

static void corner_buf_mirror_hor(lv_opa_t * sh_buf, int32_t size)
{
    int32_t y;
    for(y = 0; y < size; y++) {
        lv_opa_t * start = sh_buf;
        lv_opa_t * end = sh_buf + size - 1;
        while(start < end) {
            lv_opa_t tmp = *start;
            *start = *end;
            *end = tmp;

            start++;
            end--;
        }
        sh_buf += size;
    }
}

static void corner_buf_flip_ver(lv_opa_t * sh_buf, int32_t size)
{
    lv_opa_t * top = sh_buf;
    lv_opa_t * bottom = sh_buf + (size - 1) * size;
    while(top < bottom) {
        int32_t x;
        for(x = 0; x < size; x++) {
            lv_opa_t tmp = top[x];
            top[x] = bottom[x];
            bottom[x] = tmp;
        }
        top += size;
        bottom -= size;
    }
}

/**
 * Calculate the blurred top right corner of a shadow.
 *
 * The shadow is the core rectangle blurred by two box filters, one after the other,
 * horizontally and vertically. Two box filters together are a trapezoid shaped filter.
 * Its second difference has only 4 non-zero values, so each blurred pixel comes from
 * 4 pixels and two running sums, whatever the width of the shadow is.
 *
 * Only the rows of the rounded corners are different from each other. These are blurred
 * horizontally first. The vertical blur then reads whole rows and writes the result.
 * @param buf       a buffer of `(sw + r) * (sw + r)` bytes for the opacity of the pixels
 * @param sw        width of the shadow
 * @param r         radius of the rectangle to blur
 * @param core_w    width of the rectangle to blur
 * @param core_h    height of the rectangle to blur
 * @return          false: there was not enough memory
 */
static bool LV_ATTRIBUTE_FAST_MEM corner_buf_calc(lv_opa_t * buf, int32_t sw, int32_t r, int32_t core_w,
                                                  int32_t core_h)
{
    int32_t size = sw + r;

    /*Widths of the two box filters*/
    int32_t box1 = LV_MAX(sw / 2, 1);
    int32_t box2 = LV_MAX(sw - sw / 2, 1);

    /*How many pixels the filter reaches from a pixel towards the outside of the shadow.
     *A box with an even width can't be centered: it reaches less to the outside.*/
    int32_t reach_x = (box1 - 1) / 2 + (box2 - 1) / 2;
    int32_t reach_y = box1 + box2 - 2 - reach_x;

    uint32_t norm = (1 << SHADOW_NORM_SHIFT) / (box1 * box2);
    uint32_t round = 1 << (SHADOW_NORM_SHIFT - 1);

    /*The rectangle to blur in the buffer*/
    lv_area_t core;
    core.x2 = sw / 2 + r - 1 - ((sw & 1) ? 0 : 1);
    core.y1 = sw / 2 + 1;
    core.x1 = core.x2 - core_w + 1;
    core.y2 = core.y1 + core_h - 1;

    /*A row of the mask with the pixels that the filter reaches on its two sides*/
    int32_t line_ofs = reach_x + 1;
    int32_t line_len = line_ofs + core.x2 + box1 + box2 + 1;

    /*Horizontally blurred rows: one for each row of the rounded corner, one for the
     *straight edge, and an empty one*/
    int32_t * sums = lv_malloc(2 * size * sizeof(int32_t) + (r + 2) * size + line_len);
    LV_ASSERT_MALLOC(sums);
    if(sums == NULL) return false;
    int32_t * diffs = sums + size;
    lv_opa_t * rows = (lv_opa_t *)(diffs + size);
    lv_opa_t * row_straight = rows + r * size;
    lv_opa_t * row_empty = row_straight + size;
    lv_opa_t * line = row_empty + size;

    lv_draw_sw_mask_radius_param_t mask_param;
    lv_draw_sw_mask_radius_init(&mask_param, &core, r, false);

    /*The last row the vertical filter reads*/
    int32_t row_last = size - 1 - reach_y + box1 + box2 - 2;

    int32_t x;
    int32_t i;
    for(i = 0; i <= r; i++) {
        /*The bottom half of a small core mirrors the top half*/
        if(core.y1 + i > row_last || core.y1 + i > core.y2 - i) break;

        lv_memset(line, 0xff, line_len);
        lv_draw_sw_mask_res_t mask_res = mask_param.dsc.cb(line, -line_ofs, core.y1 + i, line_len, &mask_param);
        if(mask_res == LV_DRAW_SW_MASK_RES_TRANSP) lv_memzero(line, line_len);

        /*Go from the right, where both the mask and the sums are zero*/
        lv_opa_t * row = rows + i * size;
        lv_memzero(row, size);
        int32_t sum = 0;
        int32_t diff = 0;
        const lv_opa_t * p = line + line_ofs + core.x2;
        for(x = core.x2 + 1 + reach_x; x >= 0; x--, p--) {
            sum -= diff;
            diff -= p[box1 + box2] - p[box1] - p[box2] + p[0];
            row[x] = ((uint32_t)sum * norm + round) >> SHADOW_NORM_SHIFT;
        }
    }
    lv_draw_sw_mask_free_param(&mask_param);
    lv_memzero(row_empty, size);

    /*The vertical blur goes from the top, where both the rows and the sums are zero.
     *The filter reaches the core first in row `y_first`.*/
    lv_memzero(sums, 2 * size * sizeof(int32_t));
    int32_t y_first = core.y1 - reach_x;
    lv_memzero(buf, y_first * size);

    int32_t y;
    for(y = y_first; y < size; y++) {
        /*The 4 rows where the second difference of the filter is not zero*/
        const lv_opa_t * taps[4];
        int32_t tap_y = y - reach_y - 2;
        for(i = 0; i < 4; i++) {
            int32_t row_y = tap_y;
            if(i == 1) row_y += box1;
            else if(i == 2) row_y += box2;
            else if(i == 3) row_y += box1 + box2;

            if(row_y < core.y1 || row_y > core.y2) {
                taps[i] = row_empty;
            }
            else {
                int32_t edge_dist = LV_MIN(row_y - core.y1, core.y2 - row_y);
                taps[i] = edge_dist < r ? rows + edge_dist * size : row_straight;
            }
        }

        lv_opa_t * out = buf + y * size;
        if((taps[0] == taps[1] && taps[2] == taps[3]) || (taps[0] == taps[2] && taps[1] == taps[3])) {
            /*The rows cancel each other*/
            for(x = 0; x < size; x++) {
                sums[x] += diffs[x];
                out[x] = ((uint32_t)sums[x] * norm + round) >> SHADOW_NORM_SHIFT;
            }
        }
        else {
            for(x = 0; x < size; x++) {
                diffs[x] += taps[3][x] - taps[2][x] - taps[1][x] + taps[0][x];
                sums[x] += diffs[x];
                out[x] = ((uint32_t)sums[x] * norm + round) >> SHADOW_NORM_SHIFT;
            }
        }
    }

    lv_free(sums);
    return true;
}

#else /*LV_DRAW_SW_COMPLEX*/

void lv_draw_sw_box_shadow(lv_layer_t * layer, const lv_area_t * clip_area, const lv_draw_box_shadow_dsc_t * dsc,
                           const lv_area_t * coords)
{
    LV_UNUSED(layer);
    LV_UNUSED(clip_area);
    LV_UNUSED(dsc);
    LV_UNUSED(coords);

    LV_LOG_WARN("LV_DRAW_SW_COMPLEX needs to be enabled");
}

#endif /*LV_DRAW_SW_COMPLEX*/

#endif /*LV_DRAW_USE_SW*/
