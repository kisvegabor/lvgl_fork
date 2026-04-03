/**
 * @file lv_obj_pos.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../misc/lv_area_private.h"
#include "../layouts/lv_layout_private.h"
#include "lv_obj_event_private.h"
#include "lv_obj_draw_private.h"
#include "lv_obj_style_private.h"
#include "lv_obj_private.h"
#include "../display/lv_display.h"
#include "../display/lv_display_private.h"
#include "lv_refr_private.h"
#include "../core/lv_global.h"

/*********************
 *      DEFINES
 *********************/
#define MY_CLASS (&lv_obj_class)
#define update_layout_mutex LV_GLOBAL_DEFAULT()->layout_update_mutex

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int32_t calc_content_width(lv_obj_t * obj);
static int32_t calc_content_height(lv_obj_t * obj);
static void update_children_coordinates(lv_obj_t * obj);
static void update_coordinates(lv_obj_t * obj);
static void transform_point_array(const lv_obj_t * obj, lv_point_t * p, size_t p_count, bool inv);
static bool is_transformed(const lv_obj_t * obj);
static lv_result_t invalidate_area_core(const lv_obj_t * obj, lv_area_t * area_tmp);
static lv_result_t obj_invalidate_area_internal(const lv_display_t * disp, const lv_obj_t * obj,
                                                const lv_area_t * area);
static void obj_move_to(lv_obj_t * obj, int32_t x, int32_t y);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_obj_set_pos(lv_obj_t * obj, int32_t x, int32_t y)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_set_x(obj, x);
    lv_obj_set_y(obj, y);
}

void lv_obj_set_x(lv_obj_t * obj, int32_t x)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_style_res_t res_x;
    lv_style_value_t v_x;

    res_x = lv_obj_get_local_style_prop(obj, LV_STYLE_X, &v_x, 0);

    if((res_x == LV_STYLE_RES_FOUND && v_x.num != x) || res_x == LV_STYLE_RES_NOT_FOUND) {
        lv_obj_set_style_x(obj, x, 0);
    }
}

void lv_obj_set_y(lv_obj_t * obj, int32_t y)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_style_res_t res_y;
    lv_style_value_t v_y;

    res_y = lv_obj_get_local_style_prop(obj, LV_STYLE_Y, &v_y, 0);

    if((res_y == LV_STYLE_RES_FOUND && v_y.num != y) || res_y == LV_STYLE_RES_NOT_FOUND) {
        lv_obj_set_style_y(obj, y, 0);
    }
}

int32_t lv_obj_calc_dynamic_width(lv_obj_t * obj, lv_style_prop_t prop)
{
    LV_ASSERT(prop == LV_STYLE_WIDTH || prop == LV_STYLE_MIN_WIDTH || prop == LV_STYLE_MAX_WIDTH);

    int32_t width = lv_obj_get_style_prop(obj, 0, prop).num;
    if(LV_COORD_IS_PCT(width)) {
        lv_obj_t * parent = lv_obj_get_parent(obj);
        int32_t parent_width = lv_obj_get_content_width(parent);
        width = (LV_COORD_GET_PCT(width) * parent_width) / 100;
        width -= lv_obj_get_style_margin_left(obj, LV_PART_MAIN) + lv_obj_get_style_margin_right(obj, LV_PART_MAIN);
    }
    return width;
}

int32_t lv_obj_calc_dynamic_height(lv_obj_t * obj, lv_style_prop_t prop)
{
    LV_ASSERT(prop == LV_STYLE_HEIGHT || prop == LV_STYLE_MIN_HEIGHT || prop == LV_STYLE_MAX_HEIGHT);

    int32_t height = lv_obj_get_style_prop(obj, 0, prop).num;
    if(LV_COORD_IS_PCT(height)) {
        lv_obj_t * parent = lv_obj_get_parent(obj);
        int32_t parent_height = lv_obj_get_content_height(parent);
        height = (LV_COORD_GET_PCT(height) * parent_height) / 100;
        height -= lv_obj_get_style_margin_top(obj, LV_PART_MAIN) + lv_obj_get_style_margin_bottom(obj, LV_PART_MAIN);
    }
    return height;

}


static void update_coordinates_init(lv_obj_t * obj)
{
    obj->w_layout = 0;
    obj->h_layout = 0;
    obj->child_coords_changed = 0;
    obj->coords_changed = 0;

    /*If there is no parent it can't control the layout either*/
    lv_obj_t * parent = obj->parent;
    if(parent == NULL) return;

    /*If there is no layout on the parent the child size not layout controlled for sure*/
    lv_layout_t layout = lv_obj_get_style_layout(parent, 0);
    if(layout == LV_LAYOUT_NONE) return;

    /*IF hidden, floating, etc, it's not affected by layout*/
    if(!lv_obj_is_layout_positioned(obj)) return;

    if(layout == LV_LAYOUT_FLEX) {
        int32_t grow = lv_obj_get_style_flex_grow(obj, 0);
        if(grow > 0) {
            lv_flex_flow_t flow = lv_obj_get_style_flex_flow(parent, 0);
            if(flow & LV_FLEX_FLOW_COLUMN) obj->h_layout = 1;
            else obj->w_layout = 1;
        }
    }
#if LV_USE_GRID
    else if(layout == LV_LAYOUT_GRID) {
        if(lv_obj_get_style_grid_cell_x_align(obj, 0) == LV_GRID_ALIGN_STRETCH) obj->w_layout = 1;
        if(lv_obj_get_style_grid_cell_y_align(obj, 0) == LV_GRID_ALIGN_STRETCH) obj->h_layout = 1;
    }
#endif
}

static void update_fixed_and_pct_size(lv_obj_t * obj)
{
    lv_obj_t * parent = lv_obj_get_parent(obj);

    /*Don't set the size of the screen as they always cover the whole display*/
    if(parent == NULL) return;

    /*Don't update layout and content width now*/
    int32_t width = lv_obj_get_style_width(obj, 0);

    if(obj->w_layout == 0 && width != LV_SIZE_CONTENT) {
        /*Just use the width, handle percentage values and clamp*/
        width = lv_obj_calc_dynamic_width(obj, LV_STYLE_WIDTH);
        int32_t min_width = lv_obj_calc_dynamic_width(obj, LV_STYLE_MIN_WIDTH);
        int32_t max_width = lv_obj_calc_dynamic_width(obj, LV_STYLE_MAX_WIDTH);
        width = LV_CLAMP(min_width, width, max_width);
        if(lv_area_get_width(&obj->coords) != width) {
            lv_obj_invalidate(obj);
            lv_area_set_width(&obj->coords, width);
            obj->coords_changed = 1;
            parent->child_coords_changed = 1;
        }
    }

    /*Don't update layout and content width now*/
    int32_t height = lv_obj_get_style_height(obj, 0);
    if(obj->h_layout == 0 && height != LV_SIZE_CONTENT) {
        /*Just use the height, handle percentage values and clamp*/
        height = lv_obj_calc_dynamic_height(obj, LV_STYLE_HEIGHT);
        int32_t min_height = lv_obj_calc_dynamic_height(obj, LV_STYLE_MIN_HEIGHT);
        int32_t max_height = lv_obj_calc_dynamic_height(obj, LV_STYLE_MAX_HEIGHT);
        height = LV_CLAMP(min_height, height, max_height);
        if(lv_area_get_height(&obj->coords) != height) {
            lv_obj_invalidate(obj);
            lv_area_set_height(&obj->coords, height);
            obj->coords_changed = 1;
            parent->child_coords_changed = 1;
        }
    }
}

void lv_obj_set_size(lv_obj_t * obj, int32_t w, int32_t h)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_set_width(obj, w);
    lv_obj_set_height(obj, h);
}

void lv_obj_set_width(lv_obj_t * obj, int32_t w)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_style_res_t res_w;
    lv_style_value_t v_w;

    res_w = lv_obj_get_local_style_prop(obj, LV_STYLE_WIDTH, &v_w, 0);

    if((res_w == LV_STYLE_RES_FOUND && v_w.num != w) || res_w == LV_STYLE_RES_NOT_FOUND) {
        lv_obj_set_style_width(obj, w, 0);
    }
}

void lv_obj_set_height(lv_obj_t * obj, int32_t h)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);
    lv_style_res_t res_h;
    lv_style_value_t v_h;

    res_h = lv_obj_get_local_style_prop(obj, LV_STYLE_HEIGHT, &v_h, 0);

    if((res_h == LV_STYLE_RES_FOUND && v_h.num != h) || res_h == LV_STYLE_RES_NOT_FOUND) {
        lv_obj_set_style_height(obj, h, 0);
    }
}

void lv_obj_set_content_width(lv_obj_t * obj, int32_t w)
{
    int32_t left = lv_obj_get_style_space_left(obj, LV_PART_MAIN);
    int32_t right = lv_obj_get_style_space_right(obj, LV_PART_MAIN);
    lv_obj_set_width(obj, w + left + right);
}

void lv_obj_set_content_height(lv_obj_t * obj, int32_t h)
{
    int32_t top = lv_obj_get_style_space_top(obj, LV_PART_MAIN);
    int32_t bottom = lv_obj_get_style_space_bottom(obj, LV_PART_MAIN);
    lv_obj_set_height(obj, h + top + bottom);
}

void lv_obj_set_layout(lv_obj_t * obj, uint32_t layout)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_set_style_layout(obj, layout, 0);

    lv_obj_mark_layout_as_dirty(obj);
}

bool lv_obj_is_layout_positioned(const lv_obj_t * obj)
{
    if(lv_obj_has_flag_any(obj, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING)) return false;

    lv_obj_t * parent = lv_obj_get_parent(obj);
    if(parent == NULL) return false;

    uint32_t layout = lv_obj_get_style_layout(parent, LV_PART_MAIN);
    if(layout) return true;
    else return false;
}

void lv_obj_mark_layout_as_dirty(lv_obj_t * obj)
{
    obj->coords_invalid = 1;
    obj->child_coords_might_change = 1;

    lv_obj_t * parent = lv_obj_get_parent(obj);
    if(parent) parent->child_coords_might_change = 1;
    while(parent) {
        if(lv_obj_get_style_width(parent, 0) == LV_SIZE_CONTENT || lv_obj_get_style_height(parent, 0) == LV_SIZE_CONTENT) {
            parent->child_coords_might_change = 1;
        }
        else {
            break;
        }
        parent = lv_obj_get_parent(parent);
    }

    /*Mark the screen as dirty too to mark that there is something to do on this screen*/
    lv_obj_t * scr = lv_obj_get_screen(obj);

    /*Make the display refreshing*/
    if(scr->scr_layout_inv == 0) {
        lv_display_t * disp = lv_obj_get_display(scr);
        lv_display_send_event(disp, LV_EVENT_REFR_REQUEST, NULL);
    }

    scr->scr_layout_inv = 1;
}

void lv_obj_update_layout(lv_obj_t * obj)
{
    if(update_layout_mutex) {
        LV_LOG_TRACE("Already running, returning");
        return;
    }

    LV_PROFILER_LAYOUT_BEGIN;
    update_layout_mutex = true;

    lv_obj_t * scr = lv_obj_get_screen(obj);
    /*Repeat until there are no more layout invalidations*/
    lv_obj_t * parent = obj;
    lv_obj_t * start_obj = obj;
    while(parent) {
        if(parent->coords_invalid || parent->child_coords_might_change) {
            start_obj = parent;
        }
        parent = lv_obj_get_parent(parent);
    }

    if(scr->scr_layout_inv) {
        LV_LOG_TRACE("Layout update begin");
        update_coordinates(start_obj);

        LV_LOG_TRACE("Layout update end");
    }

    /*Mark the screen as clean only if its was updated. Otherwise smaller indirect children might remain invalid */
    if(start_obj == scr) scr->scr_layout_inv = 0;

    lv_display_t * disp = lv_obj_get_display(scr);
    lv_display_send_event(disp, LV_EVENT_UPDATE_LAYOUT_COMPLETED, NULL);
    update_layout_mutex = false;
    LV_PROFILER_LAYOUT_END;
}

void lv_obj_set_align(lv_obj_t * obj, lv_align_t align)
{
    lv_obj_set_style_align(obj, align, 0);
}

void lv_obj_align(lv_obj_t * obj, lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    lv_obj_set_style_align(obj, align, 0);
    lv_obj_set_pos(obj, x_ofs, y_ofs);
}

void lv_obj_align_to(lv_obj_t * obj, lv_obj_t * base, lv_align_t align, int32_t x_ofs, int32_t y_ofs)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_update_layout(base);
    lv_obj_update_layout(obj);
    if(base == NULL) base = lv_obj_get_parent(obj);

    LV_ASSERT_OBJ(base, MY_CLASS);

    int32_t x = 0;
    int32_t y = 0;

    lv_obj_t * parent = lv_obj_get_parent(obj);

    LV_ASSERT_OBJ(parent, MY_CLASS);

    int32_t pleft = lv_obj_get_style_space_left(parent, LV_PART_MAIN);
    int32_t ptop = lv_obj_get_style_space_top(parent, LV_PART_MAIN);

    int32_t bleft = lv_obj_get_style_space_left(base, LV_PART_MAIN);
    int32_t btop = lv_obj_get_style_space_top(base, LV_PART_MAIN);

    if(align == LV_ALIGN_DEFAULT) {
        if(lv_obj_get_style_base_dir(base, LV_PART_MAIN) == LV_BASE_DIR_RTL) align = LV_ALIGN_TOP_RIGHT;
        else align = LV_ALIGN_TOP_LEFT;
    }

    switch(align) {
        case LV_ALIGN_CENTER:
            x = lv_obj_get_content_width(base) / 2 - lv_obj_get_width(obj) / 2 + bleft;
            y = lv_obj_get_content_height(base) / 2 - lv_obj_get_height(obj) / 2 + btop;
            break;

        case LV_ALIGN_TOP_LEFT:
            x = bleft;
            y = btop;
            break;

        case LV_ALIGN_TOP_MID:
            x = lv_obj_get_content_width(base) / 2 - lv_obj_get_width(obj) / 2 + bleft;
            y = btop;
            break;

        case LV_ALIGN_TOP_RIGHT:
            x = lv_obj_get_content_width(base) - lv_obj_get_width(obj) + bleft;
            y = btop;
            break;

        case LV_ALIGN_BOTTOM_LEFT:
            x = bleft;
            y = lv_obj_get_content_height(base) - lv_obj_get_height(obj) + btop;
            break;
        case LV_ALIGN_BOTTOM_MID:
            x = lv_obj_get_content_width(base) / 2 - lv_obj_get_width(obj) / 2 + bleft;
            y = lv_obj_get_content_height(base) - lv_obj_get_height(obj) + btop;
            break;

        case LV_ALIGN_BOTTOM_RIGHT:
            x = lv_obj_get_content_width(base) - lv_obj_get_width(obj) + bleft;
            y = lv_obj_get_content_height(base) - lv_obj_get_height(obj) + btop;
            break;

        case LV_ALIGN_LEFT_MID:
            x = bleft;
            y = lv_obj_get_content_height(base) / 2 - lv_obj_get_height(obj) / 2 + btop;
            break;

        case LV_ALIGN_RIGHT_MID:
            x = lv_obj_get_content_width(base) - lv_obj_get_width(obj) + bleft;
            y = lv_obj_get_content_height(base) / 2 - lv_obj_get_height(obj) / 2 + btop;
            break;

        case LV_ALIGN_OUT_TOP_LEFT:
            x = 0;
            y = -lv_obj_get_height(obj);
            break;

        case LV_ALIGN_OUT_TOP_MID:
            x = lv_obj_get_width(base) / 2 - lv_obj_get_width(obj) / 2;
            y = -lv_obj_get_height(obj);
            break;

        case LV_ALIGN_OUT_TOP_RIGHT:
            x = lv_obj_get_width(base) - lv_obj_get_width(obj);
            y = -lv_obj_get_height(obj);
            break;

        case LV_ALIGN_OUT_BOTTOM_LEFT:
            x = 0;
            y = lv_obj_get_height(base);
            break;

        case LV_ALIGN_OUT_BOTTOM_MID:
            x = lv_obj_get_width(base) / 2 - lv_obj_get_width(obj) / 2;
            y = lv_obj_get_height(base);
            break;

        case LV_ALIGN_OUT_BOTTOM_RIGHT:
            x = lv_obj_get_width(base) - lv_obj_get_width(obj);
            y = lv_obj_get_height(base);
            break;

        case LV_ALIGN_OUT_LEFT_TOP:
            x = -lv_obj_get_width(obj);
            y = 0;
            break;

        case LV_ALIGN_OUT_LEFT_MID:
            x = -lv_obj_get_width(obj);
            y = lv_obj_get_height(base) / 2 - lv_obj_get_height(obj) / 2;
            break;

        case LV_ALIGN_OUT_LEFT_BOTTOM:
            x = -lv_obj_get_width(obj);
            y = lv_obj_get_height(base) - lv_obj_get_height(obj);
            break;

        case LV_ALIGN_OUT_RIGHT_TOP:
            x = lv_obj_get_width(base);
            y = 0;
            break;

        case LV_ALIGN_OUT_RIGHT_MID:
            x = lv_obj_get_width(base);
            y = lv_obj_get_height(base) / 2 - lv_obj_get_height(obj) / 2;
            break;

        case LV_ALIGN_OUT_RIGHT_BOTTOM:
            x = lv_obj_get_width(base);
            y = lv_obj_get_height(base) - lv_obj_get_height(obj);
            break;

        case LV_ALIGN_DEFAULT:
            break;
    }

    if(LV_COORD_IS_PCT(x_ofs)) x_ofs = (lv_obj_get_width(base) * LV_COORD_GET_PCT(x_ofs)) / 100;
    if(LV_COORD_IS_PCT(y_ofs)) y_ofs = (lv_obj_get_height(base) * LV_COORD_GET_PCT(y_ofs)) / 100;
    if(lv_obj_get_style_base_dir(parent, LV_PART_MAIN) == LV_BASE_DIR_RTL) {
        x += x_ofs + base->coords.x1 - parent->coords.x1 + lv_obj_get_scroll_right(parent) - pleft;
    }
    else {
        x += x_ofs + base->coords.x1 - parent->coords.x1 + lv_obj_get_scroll_left(parent) - pleft;
    }
    y += y_ofs + base->coords.y1 - parent->coords.y1 + lv_obj_get_scroll_top(parent) - ptop;
    lv_obj_set_style_align(obj, LV_ALIGN_TOP_LEFT, 0);
    lv_obj_set_pos(obj, x, y);

}

void lv_obj_get_coords(const lv_obj_t * obj, lv_area_t * coords)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_area_copy(coords, &obj->coords);
}

int32_t lv_obj_get_x(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t rel_x;
    lv_obj_t * parent = lv_obj_get_parent(obj);
    if(parent) {
        rel_x  = obj->coords.x1 - parent->coords.x1;
        rel_x += lv_obj_get_scroll_x(parent);
        rel_x -= lv_obj_get_style_space_left(parent, LV_PART_MAIN);
    }
    else {
        rel_x = obj->coords.x1;
    }
    return rel_x;
}

int32_t lv_obj_get_x2(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    return lv_obj_get_x(obj) + lv_obj_get_width(obj);
}

int32_t lv_obj_get_y(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t rel_y;
    lv_obj_t * parent = lv_obj_get_parent(obj);
    if(parent) {
        rel_y = obj->coords.y1 - parent->coords.y1;
        rel_y += lv_obj_get_scroll_y(parent);
        rel_y -= lv_obj_get_style_space_top(parent, LV_PART_MAIN);
    }
    else {
        rel_y = obj->coords.y1;
    }
    return rel_y;
}

int32_t lv_obj_get_y2(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    return lv_obj_get_y(obj) + lv_obj_get_height(obj);
}

int32_t lv_obj_get_x_aligned(const lv_obj_t * obj)
{
    return lv_obj_get_style_x(obj, LV_PART_MAIN);
}

int32_t lv_obj_get_y_aligned(const lv_obj_t * obj)
{
    return lv_obj_get_style_y(obj, LV_PART_MAIN);
}

int32_t lv_obj_get_width(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    return lv_area_get_width(&obj->coords);
}

int32_t lv_obj_get_height(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    return lv_area_get_height(&obj->coords);
}

int32_t lv_obj_get_content_width(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t left = lv_obj_get_style_space_left(obj, LV_PART_MAIN);
    int32_t right = lv_obj_get_style_space_right(obj, LV_PART_MAIN);

    return lv_obj_get_width(obj) - left - right;
}

int32_t lv_obj_get_content_height(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t top = lv_obj_get_style_space_top(obj, LV_PART_MAIN);
    int32_t bottom = lv_obj_get_style_space_bottom(obj, LV_PART_MAIN);

    return lv_obj_get_height(obj) - top - bottom;
}

void lv_obj_get_content_coords(const lv_obj_t * obj, lv_area_t * area)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_get_coords(obj, area);
    area->x1 += lv_obj_get_style_space_left(obj, LV_PART_MAIN);
    area->x2 -= lv_obj_get_style_space_right(obj, LV_PART_MAIN);
    area->y1 += lv_obj_get_style_space_top(obj, LV_PART_MAIN);
    area->y2 -= lv_obj_get_style_space_bottom(obj, LV_PART_MAIN);

}

int32_t lv_obj_get_self_width(const lv_obj_t * obj)
{
    lv_point_t p = {0, LV_COORD_MIN};
    lv_obj_send_event((lv_obj_t *)obj, LV_EVENT_GET_SELF_SIZE, &p);
    return p.x;
}

int32_t lv_obj_get_self_height(const lv_obj_t * obj)
{
    lv_point_t p = {LV_COORD_MIN, 0};
    lv_obj_send_event((lv_obj_t *)obj, LV_EVENT_GET_SELF_SIZE, &p);
    return p.y;
}


bool lv_obj_is_style_any_width_content(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t w = lv_obj_get_style_width(obj, LV_PART_MAIN);
    int32_t minw = lv_obj_get_style_min_width(obj, LV_PART_MAIN);
    int32_t maxw = lv_obj_get_style_max_width(obj, LV_PART_MAIN);
    return (w == LV_SIZE_CONTENT || minw == LV_SIZE_CONTENT || maxw == LV_SIZE_CONTENT);
}

bool lv_obj_is_style_any_height_content(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    int32_t h = lv_obj_get_style_height(obj, LV_PART_MAIN);
    int32_t minh = lv_obj_get_style_min_height(obj, LV_PART_MAIN);
    int32_t maxh = lv_obj_get_style_max_height(obj, LV_PART_MAIN);
    return (h == LV_SIZE_CONTENT || minh == LV_SIZE_CONTENT || maxh == LV_SIZE_CONTENT);
}

bool lv_obj_refresh_self_size(lv_obj_t * obj)
{
    if(!lv_obj_is_style_any_width_content(obj) && !lv_obj_is_style_any_height_content(obj))
        return false;

    /**
     * Refresh the parent's layout, because the children size is in some way dependent on its contents we need to force a
     * recalculation of the parents layout
     */
    lv_obj_t * parent = lv_obj_get_parent(obj);
    if(parent != NULL) {
        parent->w_layout = 0;
        parent->h_layout = 0;
        lv_obj_mark_layout_as_dirty(parent);
    }
    lv_obj_mark_layout_as_dirty(obj);
    return true;
}

static void update_content_size(lv_obj_t * obj)
{
    lv_obj_t * parent = lv_obj_get_parent(obj);
    /*Don't set the size of the screen as they always cover the whole display*/
    if(parent == NULL) return;

    /*If the width or height is set by a layout do not modify them*/
    int32_t width = lv_obj_get_style_width(obj, LV_PART_MAIN);
    if(!obj->w_layout && width == LV_SIZE_CONTENT) {
        width = calc_content_width(obj);
        int32_t minw = lv_obj_calc_dynamic_width(obj, LV_STYLE_MIN_WIDTH);
        int32_t maxw = lv_obj_calc_dynamic_width(obj, LV_STYLE_MAX_WIDTH);
        width = LV_CLAMP(minw, width, maxw);

        /**
         * If the object style (after clamping) results in a width that is defined as a percentage of the parent,
         * and if the parent's width is set to LV_SIZE_CONTENT and not managed by a layout, this object should not
         * influence the parent's content width calculation. Thus, the `w_ignore_size` flag is set accordingly.
         */
        int32_t w_style;
        if(width == minw) {
            w_style = lv_obj_get_style_min_width(obj, LV_PART_MAIN);
        }
        else if(width == maxw) {
            w_style = lv_obj_get_style_max_width(obj, LV_PART_MAIN);
        }
        else {
            w_style = lv_obj_get_style_width(obj, LV_PART_MAIN);
        }
        obj->w_ignore_size =
            (LV_COORD_IS_PCT(w_style) && parent->w_layout == 0 && lv_obj_get_style_width(parent, 0) == LV_SIZE_CONTENT);

        if(lv_area_get_width(&obj->coords) != width) {
            lv_obj_invalidate(obj);
            lv_area_set_width(&obj->coords, width);

            obj->coords_changed = 1;
            parent->child_coords_changed = 1;
        }
    }

    int32_t height = lv_obj_get_style_height(obj, LV_PART_MAIN);;
    if(!obj->h_layout && height == LV_SIZE_CONTENT) {
        height = calc_content_height(obj);
        int32_t minh = lv_obj_calc_dynamic_height(obj, LV_STYLE_MIN_HEIGHT);
        int32_t maxh = lv_obj_calc_dynamic_height(obj, LV_STYLE_MAX_HEIGHT);
        height = LV_CLAMP(minh, height, maxh);

        /**
         * If the object style (after clamping) results in a height that is defined as a percentage of the parent,
         * and if the parent's height is set to LV_SIZE_CONTENT and not managed by a layout, this object should not
         * influence the parent's content height calculation. Thus, the `h_ignore_size` flag is set accordingly.
         */
        int32_t h_style;
        if(height == minh) {
            h_style = lv_obj_get_style_min_height(obj, LV_PART_MAIN);
        }
        else if(height == maxh) {
            h_style = lv_obj_get_style_max_height(obj, LV_PART_MAIN);
        }
        else {
            h_style = lv_obj_get_style_height(obj, LV_PART_MAIN);
        }
        obj->h_ignore_size = (LV_COORD_IS_PCT(h_style) && parent->h_layout == 0 &&
                              lv_obj_get_style_height(parent, 0) == LV_SIZE_CONTENT);

        if(lv_area_get_height(&obj->coords) != height) {
            lv_obj_invalidate(obj);
            lv_area_set_height(&obj->coords, height);

            obj->coords_changed = 1;
            parent->child_coords_changed = 1;
        }
    }
}

static void update_align(lv_obj_t * obj)
{
    if(lv_obj_is_layout_positioned(obj)) return;

    lv_obj_t * parent = lv_obj_get_parent(obj);
    int32_t x = lv_obj_get_style_x(obj, LV_PART_MAIN);
    int32_t y = lv_obj_get_style_y(obj, LV_PART_MAIN);

    if(parent == NULL) {
        obj_move_to(obj, x, y);
        return;
    }

    /*Handle percentage value*/
    int32_t pw = lv_obj_get_content_width(parent);
    int32_t ph = lv_obj_get_content_height(parent);
    if(LV_COORD_IS_PCT(x)) {
        if(lv_obj_get_style_width(parent, LV_PART_MAIN) == LV_SIZE_CONTENT) x = 0; /*Avoid circular dependency*/
        else x = (pw * LV_COORD_GET_PCT(x)) / 100;
    }

    if(LV_COORD_IS_PCT(y)) {
        if(lv_obj_get_style_height(parent, LV_PART_MAIN) == LV_SIZE_CONTENT) y = 0; /*Avoid circular dependency*/
        else y = (ph * LV_COORD_GET_PCT(y)) / 100;
    }

    /*Handle percentage value of translate*/
    int32_t tr_x = lv_obj_get_style_translate_x(obj, LV_PART_MAIN);
    int32_t tr_y = lv_obj_get_style_translate_y(obj, LV_PART_MAIN);
    int32_t w = lv_obj_get_width(obj);
    int32_t h = lv_obj_get_height(obj);
    if(LV_COORD_IS_PCT(tr_x)) tr_x = (w * LV_COORD_GET_PCT(tr_x)) / 100;
    if(LV_COORD_IS_PCT(tr_y)) tr_y = (h * LV_COORD_GET_PCT(tr_y)) / 100;

    /*Use the translation*/
    x += tr_x;
    y += tr_y;

    lv_align_t align = lv_obj_get_style_align(obj, LV_PART_MAIN);

    if(align == LV_ALIGN_DEFAULT) {
        align = LV_ALIGN_TOP_LEFT;
    }

    bool rtl = lv_obj_get_style_base_dir(parent, LV_PART_MAIN) == LV_BASE_DIR_RTL;

    if(rtl) {
        switch(align) {
            case LV_ALIGN_TOP_LEFT:
                align = LV_ALIGN_TOP_RIGHT;
                break;
            case LV_ALIGN_TOP_RIGHT:
                align = LV_ALIGN_TOP_LEFT;
                break;
            case LV_ALIGN_LEFT_MID:
                align = LV_ALIGN_RIGHT_MID;
                break;
            case LV_ALIGN_RIGHT_MID:
                align = LV_ALIGN_LEFT_MID;
                break;
            case LV_ALIGN_BOTTOM_LEFT:
                align = LV_ALIGN_BOTTOM_RIGHT;
                break;
            case LV_ALIGN_BOTTOM_RIGHT:
                align = LV_ALIGN_BOTTOM_LEFT;
                break;
            default:
                break;
        }
    }

    switch(align) {
        case LV_ALIGN_TOP_LEFT:
            break;
        case LV_ALIGN_TOP_MID:
            x = rtl ? pw / 2 - w / 2 - x : pw / 2 - w / 2 + x;
            break;
        case LV_ALIGN_TOP_RIGHT:
            x = rtl ? pw - w - x : pw - w + x;
            break;
        case LV_ALIGN_LEFT_MID:
            y += ph / 2 - h / 2;
            break;
        case LV_ALIGN_BOTTOM_LEFT:
            y += ph - h;
            break;
        case LV_ALIGN_BOTTOM_MID:
            x = rtl ? pw / 2 - w / 2 - x : pw / 2 - w / 2 + x;
            y += ph - h;
            break;
        case LV_ALIGN_BOTTOM_RIGHT:
            x = rtl ? pw - w - x : pw - w + x;
            y += ph - h;
            break;
        case LV_ALIGN_RIGHT_MID:
            x = rtl ? pw - w - x : pw - w + x;
            y += ph / 2 - h / 2;
            break;
        case LV_ALIGN_CENTER:
            x = rtl ? pw / 2 - w / 2 - x : pw / 2 - w / 2 + x;
            y += ph / 2 - h / 2;
            break;
        default:
            break;
    }

    obj_move_to(obj, x, y);
}



void lv_obj_transform_point(const lv_obj_t * obj, lv_point_t * p, lv_obj_point_transform_flag_t flags)
{
    lv_obj_transform_point_array(obj, p, 1, flags);
}

void lv_obj_transform_point_array(const lv_obj_t * obj, lv_point_t points[], size_t count,
                                  lv_obj_point_transform_flag_t flags)
{
    if(obj) {
        lv_layer_type_t layer_type = lv_obj_get_layer_type(obj);
        bool do_tranf = layer_type == LV_LAYER_TYPE_TRANSFORM;
        bool recursive = flags & LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE;
        bool inverse = flags & LV_OBJ_POINT_TRANSFORM_FLAG_INVERSE;
        if(inverse) {
            if(recursive) lv_obj_transform_point_array(lv_obj_get_parent(obj), points, count, flags);
            if(do_tranf) transform_point_array(obj, points, count, inverse);
        }
        else {
            if(do_tranf) transform_point_array(obj, points, count, inverse);
            if(recursive) lv_obj_transform_point_array(lv_obj_get_parent(obj), points, count, flags);
        }
    }
}

void lv_obj_get_transformed_area(const lv_obj_t * obj, lv_area_t * area, lv_obj_point_transform_flag_t flags)
{
    lv_point_t p[4] = {
        {area->x1, area->y1},
        {area->x1, area->y2 + 1},
        {area->x2 + 1, area->y1},
        {area->x2 + 1, area->y2 + 1},
    };

    lv_obj_transform_point_array(obj, p, 4, flags);

    area->x1 = LV_MIN4(p[0].x, p[1].x, p[2].x, p[3].x);
    area->x2 = LV_MAX4(p[0].x, p[1].x, p[2].x, p[3].x);
    area->y1 = LV_MIN4(p[0].y, p[1].y, p[2].y, p[3].y);
    area->y2 = LV_MAX4(p[0].y, p[1].y, p[2].y, p[3].y);
}

typedef struct {
    const lv_obj_t * requester_obj;
    const lv_area_t * inv_area;
} blur_walk_data_t;

static lv_obj_tree_walk_res_t blur_walk_cb(lv_obj_t * obj, void * user_data)
{
    blur_walk_data_t * blur_data = user_data;
    if(obj == blur_data->requester_obj) return LV_OBJ_TREE_WALK_SKIP_CHILDREN;

    /*Truncate the area to the object*/
    lv_area_t obj_coords;
    int32_t ext_size = lv_obj_get_ext_draw_size(obj);
    lv_area_copy(&obj_coords, &obj->coords);
    lv_area_increase(&obj_coords, ext_size, ext_size);

    if(is_transformed(obj)) {
        lv_obj_get_transformed_area(obj, &obj_coords, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
    }

    /*If the widget has blur set, invalidate it*/
    if(lv_area_is_on(blur_data->inv_area, &obj_coords)) {
        const uint32_t group_blur = (uint32_t)1 << lv_style_get_prop_group(LV_STYLE_BLUR_RADIUS);
        const uint32_t group_dropshadow = (uint32_t)1 << lv_style_get_prop_group(LV_STYLE_DROP_SHADOW_OPA);
        const lv_state_t state = lv_obj_style_get_selector_state(lv_obj_get_state(obj));
        const lv_state_t state_inv = ~state;
        lv_style_value_t v;
        uint32_t i;
        for(i = 0; i < obj->style_cnt; i++) {
            lv_obj_style_t * obj_style = &obj->styles[i];
            if(obj_style->is_disabled) continue;

            lv_state_t state_style = lv_obj_style_get_selector_state(obj->styles[i].selector);
            if((state_style & state_inv)) continue;

            bool invalidation_needed = false;
            if((obj_style->style->has_group & group_blur) &&
               lv_style_get_prop(obj_style->style, LV_STYLE_BLUR_RADIUS, &v)) {
                invalidation_needed = true;
            }
            if((obj_style->style->has_group & group_dropshadow) &&
               lv_style_get_prop(obj_style->style, LV_STYLE_DROP_SHADOW_OPA, &v)) {
                invalidation_needed = true;
            }

            if(invalidation_needed == false) continue;

            /*Truncate the area to the object*/
            ext_size = lv_obj_get_ext_draw_size(obj);
            lv_area_copy(&obj_coords, &obj->coords);
            obj_coords.x1 -= ext_size;
            obj_coords.y1 -= ext_size;
            obj_coords.x2 += ext_size;
            obj_coords.y2 += ext_size;

            invalidate_area_core(obj, &obj_coords);

            /*No need to check the children as the widget is already invalidated
             *which will redraw the children too*/
            return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
        }

        /*Check the next child, maybe it's blurred*/
        return LV_OBJ_TREE_WALK_NEXT;
    }
    else {
        /*Not on the area of interest, skip it*/
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
}

lv_result_t lv_obj_invalidate_area(const lv_obj_t * obj, const lv_area_t * area)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_display_t * disp   = lv_obj_get_display(obj);
    if(!lv_display_is_invalidation_enabled(disp)) return LV_RESULT_INVALID;

    return obj_invalidate_area_internal(disp, obj, area);
}


lv_result_t lv_obj_invalidate(lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    /*Already invalidated*/
    if(obj->redraw_requested) return LV_RESULT_OK;

    obj->redraw_requested = 1;

    lv_display_t * disp = lv_obj_get_display(obj);
    if(!lv_display_is_invalidation_enabled(disp)) return LV_RESULT_INVALID;

    /*Truncate the area to the object*/
    lv_area_t obj_coords;
    int32_t ext_size = lv_obj_get_ext_draw_size(obj);
    lv_area_copy(&obj_coords, &obj->coords);
    obj_coords.x1 -= ext_size;
    obj_coords.y1 -= ext_size;
    obj_coords.x2 += ext_size;
    obj_coords.y2 += ext_size;

    lv_result_t res = obj_invalidate_area_internal(disp, obj, &obj_coords);

    return res;
}

bool lv_obj_area_is_visible(const lv_obj_t * obj, lv_area_t * area)
{
    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return false;

    /*Invalidate the object only if it belongs to the current or previous or one of the layers'*/
    lv_obj_t * obj_scr = lv_obj_get_screen(obj);
    lv_display_t * disp   = lv_obj_get_display(obj_scr);
    if(obj_scr != lv_display_get_screen_active(disp) &&
       obj_scr != lv_display_get_screen_prev(disp) &&
       obj_scr != lv_display_get_layer_bottom(disp) &&
       obj_scr != lv_display_get_layer_top(disp) &&
       obj_scr != lv_display_get_layer_sys(disp)) {
        return false;
    }

    /*Truncate the area to the object*/
    lv_area_t obj_coords;
    int32_t ext_size = lv_obj_get_ext_draw_size(obj);
    lv_area_copy(&obj_coords, &obj->coords);
    lv_area_increase(&obj_coords, ext_size, ext_size);

    /*The area is not on the object*/
    if(!lv_area_intersect(area, area, &obj_coords)) return false;

    if(is_transformed(obj)) {
        lv_obj_get_transformed_area(obj, area, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
    }

    /*Truncate recursively to the parents*/
    lv_obj_t * parent = lv_obj_get_parent(obj);
    while(parent != NULL) {
        /*If the parent is hidden then the child is hidden and won't be drawn*/
        if(lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) return false;

        /*Truncate to the parent and if no common parts break*/
        lv_area_t parent_coords = parent->coords;
        if(lv_obj_has_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
            int32_t parent_ext_size = lv_obj_get_ext_draw_size(parent);
            lv_area_increase(&parent_coords, parent_ext_size, parent_ext_size);
        }

        if(is_transformed(parent)) {
            lv_obj_get_transformed_area(parent, &parent_coords, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
        }
        if(!lv_area_intersect(area, area, &parent_coords)) return false;

        parent = lv_obj_get_parent(parent);
    }

    return true;
}

bool lv_obj_is_visible(const lv_obj_t * obj)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_area_t obj_coords;
    int32_t ext_size = lv_obj_get_ext_draw_size(obj);
    lv_area_copy(&obj_coords, &obj->coords);
    obj_coords.x1 -= ext_size;
    obj_coords.y1 -= ext_size;
    obj_coords.x2 += ext_size;
    obj_coords.y2 += ext_size;

    return lv_obj_area_is_visible(obj, &obj_coords);
}

void lv_obj_set_ext_click_area(lv_obj_t * obj, int32_t size)
{
    LV_ASSERT_OBJ(obj, MY_CLASS);

    lv_obj_allocate_spec_attr(obj);
    obj->spec_attr->ext_click_pad = size;
}

void lv_obj_get_click_area(const lv_obj_t * obj, lv_area_t * area)
{
    lv_area_copy(area, &obj->coords);
    if(obj->spec_attr) {
        lv_area_increase(area, obj->spec_attr->ext_click_pad, obj->spec_attr->ext_click_pad);
    }
}

bool lv_obj_hit_test(lv_obj_t * obj, const lv_point_t * point)
{
    if(!lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) return false;

    lv_area_t a;
    lv_obj_get_click_area(obj, &a);
    bool res = lv_area_is_point_on(&a, point, 0);
    if(res == false) return false;

    if(lv_obj_has_flag(obj, LV_OBJ_FLAG_ADV_HITTEST)) {
        lv_hit_test_info_t hit_info;
        hit_info.point = point;
        hit_info.res = true;
        lv_obj_send_event(obj, LV_EVENT_HIT_TEST, &hit_info);
        return hit_info.res;
    }

    return res;
}

void lv_obj_center(lv_obj_t * obj)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
}

void lv_obj_set_transform(lv_obj_t * obj, const lv_matrix_t * matrix)
{
#if LV_DRAW_TRANSFORM_USE_MATRIX
    LV_ASSERT_OBJ(obj, MY_CLASS);

    if(!matrix) {
        lv_obj_reset_transform(obj);
        return;
    }

    lv_obj_allocate_spec_attr(obj);
    if(!obj->spec_attr->matrix) {
        obj->spec_attr->matrix = lv_malloc(sizeof(lv_matrix_t));
        LV_ASSERT_MALLOC(obj->spec_attr->matrix);
    }

    /* Invalidate the old area */
    lv_obj_invalidate(obj);

    /* Copy the matrix */
    *obj->spec_attr->matrix = *matrix;

    /* Matrix is set. Update the layer type */
    lv_obj_update_layer_type(obj);

#else
    LV_UNUSED(obj);
    LV_UNUSED(matrix);
    LV_LOG_WARN("Transform matrix is not used because LV_DRAW_TRANSFORM_USE_MATRIX is disabled");
#endif
}

void lv_obj_reset_transform(lv_obj_t * obj)
{
#if LV_DRAW_TRANSFORM_USE_MATRIX
    LV_ASSERT_OBJ(obj, MY_CLASS);
    if(!obj->spec_attr) {
        return;
    }

    if(!obj->spec_attr->matrix) {
        return;
    }

    /* Invalidate the old area */
    lv_obj_invalidate(obj);

    /* Free the matrix */
    lv_free(obj->spec_attr->matrix);
    obj->spec_attr->matrix = NULL;

    /* Matrix is cleared. Update the layer type */
    lv_obj_update_layer_type(obj);
#else
    LV_UNUSED(obj);
#endif
}

const lv_matrix_t * lv_obj_get_transform(const lv_obj_t * obj)
{
#if LV_DRAW_TRANSFORM_USE_MATRIX
    LV_ASSERT_OBJ(obj, MY_CLASS);
    if(obj->spec_attr) {
        return obj->spec_attr->matrix;
    }
#else
    LV_UNUSED(obj);
#endif
    return NULL;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_result_t obj_invalidate_area_internal(const lv_display_t * disp, const lv_obj_t * obj,
                                                const lv_area_t * area)
{
    LV_ASSERT_NULL(disp);
    LV_ASSERT_NULL(obj);
    LV_ASSERT_NULL(area);

    lv_area_t area_tmp;
    lv_area_copy(&area_tmp, area);

    lv_result_t res = invalidate_area_core(obj, &area_tmp);
    if(res == LV_RESULT_INVALID) return res;

    /*If this area is on a blurred widget, invalidate that widget too*/
    blur_walk_data_t blur_walk_data;
    blur_walk_data.requester_obj = obj;
    blur_walk_data.inv_area = &area_tmp;
    lv_obj_tree_walk(disp->act_scr, blur_walk_cb, &blur_walk_data);
    if(disp->prev_scr) lv_obj_tree_walk(disp->prev_scr, blur_walk_cb, &blur_walk_data);
    lv_obj_tree_walk(disp->sys_layer, blur_walk_cb, &blur_walk_data);
    lv_obj_tree_walk(disp->top_layer, blur_walk_cb, &blur_walk_data);
    lv_obj_tree_walk(disp->bottom_layer, blur_walk_cb, &blur_walk_data);

    return res;
}

static bool is_transformed(const lv_obj_t * obj)
{
    while(obj) {
        if(obj->spec_attr && obj->spec_attr->layer_type == LV_LAYER_TYPE_TRANSFORM) return true;
        obj = obj->parent;
    }
    return false;
}

static int32_t calc_content_width(lv_obj_t * obj)
{
    /*Assumptions:
     * - Child sizes are already set
     * - Layout positions are applied
     * - Only normal x/y/align based alignment is not set yet*/

    int32_t scroll_x_tmp = lv_obj_get_scroll_x(obj);
    if(obj->spec_attr) obj->spec_attr->scroll.x = 0;

    int32_t space_right = lv_obj_get_style_space_right(obj, LV_PART_MAIN);
    int32_t space_left = lv_obj_get_style_space_left(obj, LV_PART_MAIN);

    int32_t self_w;
    self_w = lv_obj_get_self_width(obj) + space_left + space_right;

    int32_t child_res = LV_COORD_MIN;

    if(!lv_layout_get_min_size(obj, &child_res, true)) {
        uint32_t i;
        uint32_t child_cnt = lv_obj_get_child_count(obj);
        /*With RTL find the left most coordinate*/
        if(lv_obj_get_style_base_dir(obj, LV_PART_MAIN) == LV_BASE_DIR_RTL) {
            for(i = 0; i < child_cnt; i++) {
                int32_t child_res_tmp = LV_COORD_MIN;
                lv_obj_t * child = obj->spec_attr->children[i];
                if(lv_obj_has_flag_any(child, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING))
                    continue;

                if(child->w_ignore_size)
                    continue;

                if(!lv_obj_is_layout_positioned(child)) {
                    lv_align_t align = lv_obj_get_style_align(child, LV_PART_MAIN);
                    switch(align) {
                        case LV_ALIGN_DEFAULT:
                        case LV_ALIGN_TOP_RIGHT:
                        case LV_ALIGN_BOTTOM_RIGHT:
                        case LV_ALIGN_RIGHT_MID:
                            /*Normal right aligns. Other are ignored due to possible circular dependencies*/
                            child_res_tmp = lv_obj_get_style_x(child, 0) + lv_area_get_width(&child->coords) + space_right;
                            break;
                        default:
                            /* Consider other cases only if x=0 and use the width of the object.
                             * With x!=0 circular dependency could occur. */
                            if(lv_obj_get_style_x(child, LV_PART_MAIN) == 0) {
                                child_res_tmp = lv_area_get_width(&child->coords) + space_right;
                                child_res_tmp += lv_obj_get_style_margin_left(child, LV_PART_MAIN);
                            }
                            break;
                    }
                }
                else {
                    child_res_tmp = obj->coords.x2 - child->coords.x1 + 1;
                }
                child_res = LV_MAX(child_res, child_res_tmp + lv_obj_get_style_margin_left(child, LV_PART_MAIN));
            }
            if(child_res != LV_COORD_MIN) {
                child_res += space_left;
            }
        }
        /*Else find the right most coordinate*/
        else {
            for(i = 0; i < child_cnt; i++) {
                int32_t child_res_tmp = LV_COORD_MIN;
                lv_obj_t * child = obj->spec_attr->children[i];
                if(lv_obj_has_flag_any(child,  LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING)) continue;

                if(child->w_ignore_size)
                    continue;

                if(!lv_obj_is_layout_positioned(child)) {
                    lv_align_t align = lv_obj_get_style_align(child, LV_PART_MAIN);
                    switch(align) {
                        case LV_ALIGN_DEFAULT:
                        case LV_ALIGN_TOP_LEFT:
                        case LV_ALIGN_BOTTOM_LEFT:
                        case LV_ALIGN_LEFT_MID:
                            /*Normal left aligns.*/
                            child_res_tmp = lv_obj_get_style_x(child, 0) + lv_area_get_width(&child->coords) + space_left;
                            break;
                        default:
                            /* Consider other cases only if x=0 and use the width of the object.
                             * With x!=0 circular dependency could occur. */
                            if(lv_obj_get_style_x(child, LV_PART_MAIN) == 0) {
                                child_res_tmp = lv_area_get_width(&child->coords) + space_left;
                                child_res_tmp += lv_obj_get_style_margin_right(child, LV_PART_MAIN);
                            }
                            break;
                    }
                }
                else {
                    child_res_tmp = child->coords.x2 - obj->coords.x1 + 1;
                }

                child_res = LV_MAX(child_res, child_res_tmp + lv_obj_get_style_margin_right(child, LV_PART_MAIN));
            }

            if(child_res != LV_COORD_MIN) {
                child_res += space_right;
            }
        }
    }

    if(obj->spec_attr) {
        obj->spec_attr->scroll.x = -scroll_x_tmp;
    }

    if(child_res == LV_COORD_MIN) {
        return self_w;
    }

    return LV_MAX(child_res, self_w);
}

static int32_t calc_content_height(lv_obj_t * obj)
{
    /*Assumptions:
     * - Child sizes are already set
     * - Layout positions are applied
     * - Only normal x/y/align based alignment is not set yet*/

    int32_t scroll_y_tmp = lv_obj_get_scroll_y(obj);
    if(obj->spec_attr) obj->spec_attr->scroll.y = 0;

    int32_t space_top = lv_obj_get_style_space_top(obj, LV_PART_MAIN);
    int32_t space_bottom = lv_obj_get_style_space_bottom(obj, LV_PART_MAIN);

    int32_t self_h;
    self_h = lv_obj_get_self_height(obj) + space_top + space_bottom;

    int32_t child_res = LV_COORD_MIN;

    if(!lv_layout_get_min_size(obj, &child_res, false)) {
        uint32_t i;
        uint32_t child_cnt = lv_obj_get_child_count(obj);
        for(i = 0; i < child_cnt; i++) {
            int32_t child_res_tmp = LV_COORD_MIN;
            lv_obj_t * child = obj->spec_attr->children[i];
            if(lv_obj_has_flag_any(child, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING))
                continue;

            if(child->h_ignore_size)
                continue;

            if(!lv_obj_is_layout_positioned(child)) {
                lv_align_t align = lv_obj_get_style_align(child, LV_PART_MAIN);
                switch(align) {
                    case LV_ALIGN_DEFAULT:
                    case LV_ALIGN_TOP_RIGHT:
                    case LV_ALIGN_TOP_MID:
                    case LV_ALIGN_TOP_LEFT:
                        /*Normal top aligns. */
                        child_res_tmp = lv_obj_get_style_y(child, 0) + lv_area_get_height(&child->coords) + space_top;
                        break;
                    default:
                        /* Consider other cases only if y=0 and use the height of the object.
                         * With y!=0 circular dependency could occur. */
                        if(lv_obj_get_style_y(child, LV_PART_MAIN) == 0) {
                            child_res_tmp = lv_area_get_height(&child->coords) + space_top;
                            child_res_tmp += lv_obj_get_style_margin_top(child, LV_PART_MAIN);
                        }
                        break;
                }
            }
            else {
                child_res_tmp = child->coords.y2 - obj->coords.y1 + 1;
            }

            child_res = LV_MAX(child_res, child_res_tmp + lv_obj_get_style_margin_bottom(child, LV_PART_MAIN));
        }

        if(child_res != LV_COORD_MIN) {
            child_res += space_bottom;
        }
    }

    if(obj->spec_attr) {
        obj->spec_attr->scroll.y = -scroll_y_tmp;
    }

    if(child_res == LV_COORD_MIN) {
        return self_h;
    }

    return LV_MAX(self_h, child_res);
}

/**
 * Update coordinates of a widget and all its children where
 * @param obj
 */
static void update_coordinates(lv_obj_t * obj)
{
    update_coordinates_init(obj);
    update_fixed_and_pct_size(obj);
    update_children_coordinates(obj);

    update_align(obj);
    obj->coords_invalid = 0;
    obj->child_coords_might_change = 0;
}


/**
 * Update the size and position of the children relative to `obj`.
 * It also updates layouts and size of `obj` if it was `LV_SIZE_CONTENT` in
 * any directions. However, it doesn't update the size of `obj` if it is
 * set by a layout (e.g. flex_grow or grid cell stretch)
 * It is assumed that `update_coordinates_init(obj)` and
 * `update_fixed_and_pct_size(obj)` are called before this function.
 * @param obj   pointer to a widget whose children's coordinates
 *              needs to be updated
 */
static void update_children_coordinates(lv_obj_t * obj)
{
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    /*If the direct children didn't change the indirect children might change, so
     *check them*/
    if(obj->child_coords_might_change == false) {
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = obj->spec_attr->children[i];
            update_children_coordinates(child);
        }
    }
    /*Direct children might have changed, let's update them*/
    else {

        /*Step 1: Calculate what we can without layouts.*/
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = obj->spec_attr->children[i];
            update_coordinates_init(child);

            /*Set only fixed and percentage width and/or height now as
             *fixed doesn't depend on anything and percentage depends
             *only on the parent that is already calculated*/
            if(child->coords_invalid) {
                update_fixed_and_pct_size(child);
            }

            /*Update all the children's size and position where the size
             *doesn't depend on a layout. Is any of the sizes depend on the layout
             *the layout needs to set the size first to update the children*/
            if(child->w_layout == 0 && child->h_layout == 0) {
                update_children_coordinates(child);
            }
        }

        /*Step 2: Handle the simple case of layout sizing when only 1 size is layout controlled
         *E.g. flex_grow*/

        /*In the first iteration of size calculation set sizes depending only on the siblings in the same direction.
         *Almost every size is set here. See the next itearation for more info. */
        lv_layout_update_children_sizes(obj, 0);

        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = obj->spec_attr->children[i];

            /*The layout set the sizes where it could, now it's time to calculate missing content sizes*/
            if((child->w_layout && !child->h_layout) ||
               (!child->w_layout && child->h_layout)) {
                update_children_coordinates(child);
            }
        }

        /*Step 3: Complex layout case when both sizes are layout controlled*/

        /*The second iteration of layout sizes are needed only in some special cases.
         *E.g. when a grid row has CONTENT height it needs to know the content size of
         *the children first to set the STRETCHed items' height accordingly.
         *Only grid needs 2 iterations*/
        lv_layout_update_children_sizes(obj, 1);

        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = obj->spec_attr->children[i];

            /*The layout set all the sizes, now it's time to calculate missing content sizes*/
            /*Set the children size and position after this the CONTENT size can be set.*/
            if(child->w_layout && child->h_layout) {
                update_children_coordinates(child);
            }
        }

        /*Step 4: Finalizing*/

        /*All children sizes are set, now set their positions*/
        lv_layout_update_children_positions(obj);

        /*All children are positioned too, set the content size of the widget (not the children)*/
        update_content_size(obj);

        /*Content size is known, align the children*/
        for(uint32_t i = 0; i < child_cnt; i++) {
            lv_obj_t * child = obj->spec_attr->children[i];

            /*Size is set, set the position if defined by align, x, y (not layout)*/
            update_align(child);
        }

        if(obj->coords_changed) {
            lv_obj_send_event(obj, LV_EVENT_SIZE_CHANGED, NULL);
        }
    }

    /*All set on this widget*/
    obj->coords_invalid = 0;
    obj->child_coords_might_change = 0;
}

// invalidate

//    if(parent->readjust_scroll_after_layout) {
//        parent->readjust_scroll_after_layout = 0;
//        lv_obj_readjust_scroll(parent, LV_ANIM_OFF);
//    }
//}

static void transform_point_array(const lv_obj_t * obj, lv_point_t * p, size_t p_count, bool inv)
{
#if LV_DRAW_TRANSFORM_USE_MATRIX
    const lv_matrix_t * obj_matrix = lv_obj_get_transform(obj);
    if(obj_matrix) {
        lv_matrix_t m;
        lv_matrix_identity(&m);
        lv_matrix_translate(&m, obj->coords.x1, obj->coords.y1);
        lv_matrix_multiply(&m, obj_matrix);
        lv_matrix_translate(&m, -obj->coords.x1, -obj->coords.y1);

        if(inv) {
            lv_matrix_t inv_m;
            lv_matrix_inverse(&inv_m, &m);
            m = inv_m;
        }

        for(size_t i = 0; i < p_count; i++) {
            lv_point_precise_t p_precise = lv_point_to_precise(&p[i]);
            lv_point_precise_t res = lv_matrix_transform_precise_point(&m, &p_precise);
            p[i] = lv_point_from_precise(&res);
        }

        return;
    }
#endif /* LV_DRAW_TRANSFORM_USE_MATRIX */

    int32_t angle = lv_obj_get_style_transform_rotation(obj, LV_PART_MAIN);
    int32_t scale_x = lv_obj_get_style_transform_scale_x_safe(obj, LV_PART_MAIN);
    int32_t scale_y = lv_obj_get_style_transform_scale_y_safe(obj, LV_PART_MAIN);
    if(scale_x == 0) scale_x = 1;
    if(scale_y == 0) scale_y = 1;

    if(angle == 0 && scale_x == LV_SCALE_NONE && scale_y == LV_SCALE_NONE) return;

    lv_point_t pivot = {
        .x = lv_obj_get_style_transform_pivot_x(obj, LV_PART_MAIN),
        .y = lv_obj_get_style_transform_pivot_y(obj, LV_PART_MAIN)
    };

    if(LV_COORD_IS_PCT(pivot.x)) {
        pivot.x = (LV_COORD_GET_PCT(pivot.x) * lv_area_get_width(&obj->coords)) / 100;
    }
    if(LV_COORD_IS_PCT(pivot.y)) {
        pivot.y = (LV_COORD_GET_PCT(pivot.y) * lv_area_get_height(&obj->coords)) / 100;
    }

    pivot.x = obj->coords.x1 + pivot.x;
    pivot.y = obj->coords.y1 + pivot.y;

    if(inv) {
        angle = -angle;
        scale_x = (256 * 256 + scale_x - 1) / scale_x;
        scale_y = (256 * 256 + scale_y - 1) / scale_y;
    }

    lv_point_array_transform(p, p_count, angle, scale_x, scale_y, &pivot, !inv);
}

static lv_result_t invalidate_area_core(const lv_obj_t * obj, lv_area_t * area_tmp)
{
    if(!lv_obj_area_is_visible(obj, area_tmp)) return LV_RESULT_INVALID;
#if LV_DRAW_TRANSFORM_USE_MATRIX
    /**
     * When using the global matrix, the vertex coordinates of clip_area lose precision after transformation,
     * which can be solved by expanding the redrawing area.
     */
    lv_area_increase(area_tmp, 5, 5);
#else
    if(obj->spec_attr && obj->spec_attr->layer_type == LV_LAYER_TYPE_TRANSFORM) {
        /*Make the area slightly larger to avoid rounding errors.
         *5 is an empirical value*/
        lv_area_increase(area_tmp, 5, 5);
    }
#endif

    lv_result_t res = lv_inv_area(lv_obj_get_display(obj), area_tmp);
    return res;
}

void lv_obj_move_children_by(lv_obj_t * obj, int32_t x_diff, int32_t y_diff, bool ignore_floating)
{
    uint32_t i;
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for(i = 0; i < child_cnt; i++) {
        lv_obj_t * child = obj->spec_attr->children[i];
        if(ignore_floating && lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING)) continue;
        child->coords.x1 += x_diff;
        child->coords.y1 += y_diff;
        child->coords.x2 += x_diff;
        child->coords.y2 += y_diff;

        lv_obj_move_children_by(child, x_diff, y_diff, false);
    }
}

void obj_move_to(lv_obj_t * obj, int32_t x, int32_t y)
{
    /*Convert x and y to absolute coordinates*/
    lv_obj_t * parent = obj->parent;

    if(parent) {
        if(lv_obj_has_flag(obj, LV_OBJ_FLAG_FLOATING)) {
            x += parent->coords.x1;
            y += parent->coords.y1;
        }
        else {
            x += parent->coords.x1 - lv_obj_get_scroll_x(parent);
            y += parent->coords.y1 - lv_obj_get_scroll_y(parent);
        }

        x += lv_obj_get_style_space_left(parent, LV_PART_MAIN);
        y += lv_obj_get_style_space_top(parent, LV_PART_MAIN);
    }

    /*Calculate and set the movement*/
    lv_point_t diff;
    diff.x = x - obj->coords.x1;
    diff.y = y - obj->coords.y1;

    /*Do nothing if the position is not changed*/
    /*It is very important else recursive positioning can
     *occur without position change*/
    if(diff.x == 0 && diff.y == 0) return;

    obj->coords.x1 += diff.x;
    obj->coords.y1 += diff.y;
    obj->coords.x2 += diff.x;
    obj->coords.y2 += diff.y;

    lv_obj_move_children_by(obj, diff.x, diff.y, false);
}


