#if LV_BUILD_TEST
#include "../lvgl.h"
#include "../../lvgl_private.h"

#include "unity/unity.h"

#define CELL_W 96
#define CELL_H 92
#define COL_CNT 8
#define ROW_CNT 5

void setUp(void)
{
    /* Function run before every test */
}

void tearDown(void)
{
    /* Function run after every test */
    lv_obj_clean(lv_screen_active());
}

/**
 * Many small shadows, each in a cell that clips it.
 * The settings are combined so that the cells cover shadows cut on any side, a background
 * that is opaque, transparent or outside of its shadow, and objects smaller than the blur.
 */
void test_draw_box_shadow_edge_cases(void)
{
    /*The reference image is for the software renderer*/
#if LV_USE_DRAW_VG_LITE || LV_USE_DRAW_NANOVG
    TEST_PASS();
#else
    static const int32_t radii[] = {0, 6, 100, 3};
    static const int32_t widths[] = {12, 25, 7, 1, 16};
    static const int32_t spreads[] = {0, 4, -3};
    static const lv_point_t offsets[] = {{0, 0}, {6, 6}, {-9, 4}, {0, -12}, {45, 0}, {-50, -40}, {3, 60}};
    static const lv_opa_t bg_opas[] = {LV_OPA_COVER, LV_OPA_50, LV_OPA_TRANSP};
    static const lv_opa_t shadow_opas[] = {LV_OPA_COVER, LV_OPA_50};
    static const lv_point_t sizes[] = {{40, 30}, {30, 44}, {2, 2}, {60, 8}, {1, 20}};
    static const lv_point_t positions[] = {{28, 30}, {-10, 30}, {70, 30}, {28, -10}, {28, 70}};

    lv_obj_t * scr = lv_screen_active();

    uint32_t i;
    for(i = 0; i < COL_CNT * ROW_CNT; i++) {
        lv_obj_t * cell = lv_obj_create(scr);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, CELL_W - 2, CELL_H - 2);
        lv_obj_set_pos(cell, (i % COL_CNT) * CELL_W, (i / COL_CNT) * CELL_H);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(0xdddddd), 0);
        lv_obj_set_scrollable(cell, false);

        lv_point_t size = sizes[(i / 4) % 5];
        int32_t spread = spreads[i % 3];
        /*Keep the blurred area valid on the tiny objects*/
        if(LV_MIN(size.x, size.y) < 8 && spread < 0) spread = 0;

        lv_obj_t * obj = lv_obj_create(cell);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, size.x, size.y);
        lv_obj_set_pos(obj, positions[(i / 3) % 5].x, positions[(i / 3) % 5].y);
        lv_obj_set_style_radius(obj, radii[i % 4], 0);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0x0000ff), 0);
        lv_obj_set_style_bg_opa(obj, bg_opas[(i / 2) % 3], 0);
        lv_obj_set_style_shadow_color(obj, lv_color_hex(0xff0000), 0);
        lv_obj_set_style_shadow_width(obj, widths[i % 5], 0);
        lv_obj_set_style_shadow_spread(obj, spread, 0);
        lv_obj_set_style_shadow_offset_x(obj, offsets[i % 7].x, 0);
        lv_obj_set_style_shadow_offset_y(obj, offsets[i % 7].y, 0);
        lv_obj_set_style_shadow_opa(obj, shadow_opas[(i / 5) % 2], 0);
    }

    TEST_ASSERT_EQUAL_SCREENSHOT("draw/box_shadow_edge_cases.png");
#endif
}

#endif
