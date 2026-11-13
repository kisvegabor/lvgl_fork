#if LV_BUILD_TEST
#include "../lvgl.h"
#include "../../lvgl_private.h"


#include "unity/unity.h"

/* NanoVG's GPU rasterization produces blur edges that differ between architectures
 * beyond a usable tolerance, so it keeps a per-architecture reference set. The SW
 * renderer is architecture-stable and shares a single reference image. */
#if LV_USE_DRAW_NANOVG
    #ifndef NON_AMD64_BUILD
        #define REF_IMG_EXT ".nvg.lp64.png"
    #else
        #define REF_IMG_EXT ".nvg.lp32.png"
    #endif
#else
    #define REF_IMG_EXT ".png"
#endif

void setUp(void)
{
    /* Function run before every test */
    lv_obj_set_flex_flow(lv_screen_active(), LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(lv_screen_active(), LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);
}

void tearDown(void)
{
    /* Function run after every test */
    lv_obj_clean(lv_screen_active());
}

#define CANVAS_WIDTH 140
#define CANVAS_HEIGHT 100

static void small_canvas_render(const char * name_sub, lv_color_format_t cf, void * canvas_buf, int32_t blur_radius,
                                uint32_t corner_radius)
{
    lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, lv_draw_buf_align(canvas_buf, cf), CANVAS_WIDTH, CANVAS_HEIGHT, cf);
    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    /*A label in the background*/
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_palette_main(LV_PALETTE_RED);
    label_dsc.font = &lv_font_montserrat_14;
    label_dsc.decor = LV_TEXT_DECOR_UNDERLINE;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                     "Curabitur sed velit sed neque tristique sagittis vel et sapien.";

    lv_area_t label1_coords = {10, 10, CANVAS_WIDTH - 10, CANVAS_HEIGHT - 10};
    lv_draw_label(&layer, &label_dsc, &label1_coords);

    /*Blur the middle of the canvas*/
    lv_draw_blur_dsc_t blur_dsc;
    lv_draw_blur_dsc_init(&blur_dsc);
    blur_dsc.corner_radius = corner_radius;
    blur_dsc.blur_radius = blur_radius;

    lv_area_t fill_coords = {25, 20, CANVAS_WIDTH - 25, CANVAS_HEIGHT - 20};
    lv_draw_blur(&layer, &blur_dsc, &fill_coords);

    /*Draw a semi-transparent rectangle on the blurred area*/
    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    fill_dsc.color = lv_palette_lighten(LV_PALETTE_BLUE, 1);
    fill_dsc.radius = corner_radius;
    fill_dsc.opa = LV_OPA_30;

    lv_draw_fill(&layer, &fill_dsc, &fill_coords);

    /*Add label on the blurred area*/
    char buf[128];
    lv_snprintf(buf, sizeof(buf), "%s\nblur:%d", name_sub, blur_radius);

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_black();
    label_dsc.font = &lv_font_montserrat_14;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    label_dsc.text = buf;

    lv_area_t label2_coords = {30, 35, CANVAS_WIDTH - 30, CANVAS_HEIGHT - 35};
    lv_draw_label(&layer, &label_dsc, &label2_coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void test_blur(void)
{
    static LV_ATTRIBUTE_MEM_ALIGN uint8_t canvas_buf[20][LV_TEST_WIDTH_TO_STRIDE(CANVAS_WIDTH,
                                                                                 4) * CANVAS_HEIGHT + LV_DRAW_BUF_ALIGN];
    uint32_t corner_radius_options[4] = {0, 4, 16, 32};
    uint32_t r;

    for(r = 0; r < 4; r++) {
        lv_obj_clean(lv_screen_active());

        uint32_t radius_current = corner_radius_options[r];
        small_canvas_render("l8", LV_COLOR_FORMAT_L8, canvas_buf[0], 6, radius_current);
        small_canvas_render("rgb565", LV_COLOR_FORMAT_RGB565, canvas_buf[1], 6, radius_current);
        small_canvas_render("rgb888", LV_COLOR_FORMAT_RGB888, canvas_buf[2], 6, radius_current);
        small_canvas_render("xrgb8888", LV_COLOR_FORMAT_XRGB8888, canvas_buf[3], 6, radius_current);
        small_canvas_render("argb8888", LV_COLOR_FORMAT_ARGB8888, canvas_buf[4], 6, radius_current);

        small_canvas_render("l8", LV_COLOR_FORMAT_L8, canvas_buf[5], 12, radius_current);
        small_canvas_render("rgb565", LV_COLOR_FORMAT_RGB565, canvas_buf[6], 12, radius_current);
        small_canvas_render("rgb888", LV_COLOR_FORMAT_RGB888, canvas_buf[7], 12, radius_current);
        small_canvas_render("xrgb8888", LV_COLOR_FORMAT_XRGB8888, canvas_buf[8], 12, radius_current);
        small_canvas_render("argb8888", LV_COLOR_FORMAT_ARGB8888, canvas_buf[9], 12, radius_current);

        small_canvas_render("l8", LV_COLOR_FORMAT_L8, canvas_buf[10], 24, radius_current);
        small_canvas_render("rgb565", LV_COLOR_FORMAT_RGB565, canvas_buf[11], 24, radius_current);
        small_canvas_render("rgb888", LV_COLOR_FORMAT_RGB888, canvas_buf[12], 24, radius_current);
        small_canvas_render("xrgb8888", LV_COLOR_FORMAT_XRGB8888, canvas_buf[13], 24, radius_current);
        small_canvas_render("argb8888", LV_COLOR_FORMAT_ARGB8888, canvas_buf[14], 24, radius_current);

        small_canvas_render("l8", LV_COLOR_FORMAT_L8, canvas_buf[15], 60, radius_current);
        small_canvas_render("rgb565", LV_COLOR_FORMAT_RGB565, canvas_buf[16], 60, radius_current);
        small_canvas_render("rgb888", LV_COLOR_FORMAT_RGB888, canvas_buf[17], 60, radius_current);
        small_canvas_render("xrgb8888", LV_COLOR_FORMAT_XRGB8888, canvas_buf[18], 60, radius_current);
        small_canvas_render("argb8888", LV_COLOR_FORMAT_ARGB8888, canvas_buf[19], 60, radius_current);

        char buf[128];
        lv_snprintf(buf, sizeof(buf), "draw/draw_blur_corner_%u" REF_IMG_EXT, radius_current);
        TEST_ASSERT_EQUAL_SCREENSHOT(buf);
    }
}

#define SHIFT_AREA_WIDTH 57
#define SHIFT_AREA_HEIGHT 41

/*Draw some shapes and blur them. Everything is moved by `ofs` pixels in both directions.*/
static lv_obj_t * shifted_canvas_render(lv_color_format_t cf, void * canvas_buf, int32_t ofs, int32_t blur_radius,
                                        lv_blur_quality_t quality)
{
    lv_obj_t * canvas = lv_canvas_create(lv_screen_active());
    lv_canvas_set_buffer(canvas, lv_draw_buf_align(canvas_buf, cf), CANVAS_WIDTH, CANVAS_HEIGHT, cf);
    lv_canvas_fill_bg(canvas, lv_color_hex3(0xccc), LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    lv_draw_fill_dsc_t fill_dsc;
    lv_draw_fill_dsc_init(&fill_dsc);
    uint32_t i;
    for(i = 0; i < 6; i++) {
        fill_dsc.color = lv_palette_main((lv_palette_t)(i * 3));
        fill_dsc.radius = i * 2;
        lv_area_t fill_coords = {ofs + i * 11, ofs + i * 7, ofs + i * 11 + 13, ofs + i * 7 + 9};
        lv_draw_fill(&layer, &fill_dsc, &fill_coords);
    }

    lv_draw_blur_dsc_t blur_dsc;
    lv_draw_blur_dsc_init(&blur_dsc);
    blur_dsc.blur_radius = blur_radius;
    blur_dsc.quality = quality;

    lv_area_t blur_coords = {ofs + 8, ofs + 8, ofs + 8 + SHIFT_AREA_WIDTH - 1, ofs + 8 + SHIFT_AREA_HEIGHT - 1};
    lv_draw_blur(&layer, &blur_dsc, &blur_coords);

    lv_canvas_finish_layer(canvas, &layer);

    return canvas;
}

/*The result of the blur must not depend on where the area is in the memory*/
void test_blur_shifted(void)
{
    static LV_ATTRIBUTE_MEM_ALIGN uint8_t canvas_buf[2][LV_TEST_WIDTH_TO_STRIDE(CANVAS_WIDTH,
                                                                                4) * CANVAS_HEIGHT + LV_DRAW_BUF_ALIGN];
    static const lv_color_format_t cfs[] = {
        LV_COLOR_FORMAT_L8, LV_COLOR_FORMAT_RGB565, LV_COLOR_FORMAT_RGB888,
        LV_COLOR_FORMAT_XRGB8888, LV_COLOR_FORMAT_ARGB8888,
    };
    static const int32_t blur_radii[] = {5, 4, 12, 30};
    static const lv_blur_quality_t qualities[] = {
        LV_BLUR_QUALITY_PRECISION, LV_BLUR_QUALITY_AUTO, LV_BLUR_QUALITY_AUTO, LV_BLUR_QUALITY_SPEED,
    };

    uint32_t cf_i;
    uint32_t blur_i;
    int32_t ofs;
    for(cf_i = 0; cf_i < sizeof(cfs) / sizeof(cfs[0]); cf_i++) {
        uint32_t px_size = lv_color_format_get_size(cfs[cf_i]);
        for(blur_i = 0; blur_i < sizeof(blur_radii) / sizeof(blur_radii[0]); blur_i++) {
            lv_obj_t * canvas_ref = shifted_canvas_render(cfs[cf_i], canvas_buf[0], 0, blur_radii[blur_i],
                                                          qualities[blur_i]);
            for(ofs = 1; ofs <= 3; ofs++) {
                lv_obj_t * canvas = shifted_canvas_render(cfs[cf_i], canvas_buf[1], ofs, blur_radii[blur_i],
                                                          qualities[blur_i]);
                int32_t y;
                for(y = 0; y < SHIFT_AREA_HEIGHT; y++) {
                    const uint8_t * row_ref = lv_draw_buf_goto_xy(lv_canvas_get_draw_buf(canvas_ref), 8, 8 + y);
                    const uint8_t * row = lv_draw_buf_goto_xy(lv_canvas_get_draw_buf(canvas), 8 + ofs, 8 + ofs + y);
                    TEST_ASSERT_EQUAL_UINT8_ARRAY(row_ref, row, SHIFT_AREA_WIDTH * px_size);
                }
                lv_obj_delete(canvas);
            }
            lv_obj_delete(canvas_ref);
        }
    }
}

#endif
