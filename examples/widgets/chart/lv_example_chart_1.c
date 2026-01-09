#include "../../lv_examples.h"
#if LV_USE_CHART && LV_BUILD_EXAMPLES

/**
 * A very basic line chart
 */
void lv_example_chart_1(void)
{
    /*Create a chart*/
    lv_obj_t * chart;
    chart = lv_chart_create(lv_screen_active());
    lv_obj_set_size(chart, 400, 300);
    lv_obj_center(chart);
    lv_chart_set_type(chart, LV_CHART_TYPE_CURVE);   /*Show lines and points too*/
    lv_chart_set_point_count(chart, 15);
    lv_chart_series_t * ser1 = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_series_t * ser2 = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_SECONDARY_Y);
    lv_obj_set_style_bg_opa(chart, LV_OPA_30, LV_PART_INDICATOR);
    lv_chart_set_next_value(chart, ser1, 10);
    lv_chart_set_next_value(chart, ser1, 20);
    lv_chart_set_next_value(chart, ser1, 30);
    lv_chart_set_next_value(chart, ser1, 80);
    lv_chart_set_next_value(chart, ser1, 90);
    lv_chart_set_next_value(chart, ser1, 90);
    lv_chart_set_next_value(chart, ser1, 90);
    lv_chart_set_next_value(chart, ser1, 10);
    lv_chart_set_next_value(chart, ser1, 20);
    lv_chart_set_next_value(chart, ser1, 10);
    lv_chart_set_next_value(chart, ser1, 40);
    lv_chart_set_next_value(chart, ser1, 60);
    lv_chart_set_next_value(chart, ser1, 60);
    lv_chart_set_next_value(chart, ser1, 40);
    lv_chart_set_next_value(chart, ser1, 45);

    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, 40);
    lv_chart_set_next_value(chart, ser2, 50);
    lv_chart_set_next_value(chart, ser2, 60);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, 80);
    lv_chart_set_next_value(chart, ser2, 80);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, 45);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);
    lv_chart_set_next_value(chart, ser2, 55);
    lv_chart_set_next_value(chart, ser2, LV_CHART_POINT_NONE);



    lv_chart_refresh(chart); /*Required after direct set*/
}

#endif
