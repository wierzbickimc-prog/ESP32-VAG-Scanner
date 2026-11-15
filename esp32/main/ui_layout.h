#pragma once

#include "board_config.h"
#include "lvgl.h"

/* Layout uses an 800-pixel design grid; preserve LVGL percentage/content values.
 * The 4.3-inch panel has a little more vertical room at the same width scale. */
#define SCREEN_W 800
#define SCREEN_H (BOARD_HEIGHT * 800 / BOARD_WIDTH)

static inline int32_t ui_px(int32_t value)
{
    if (LV_COORD_IS_SPEC(value) || value == LV_RADIUS_CIRCLE)
        return value;
    int32_t scaled = (value * BOARD_WIDTH + (value < 0 ? -400 : 400)) / 800;
    return scaled == 0 && value != 0 ? (value < 0 ? -1 : 1) : scaled;
}

#define S(value) ui_px(value)

#if CONFIG_BOARD_JC4880P443
#define GUI_FONT_16 lv_font_montserrat_12
#define GUI_FONT_20 lv_font_montserrat_14
#define GUI_FONT_24 lv_font_montserrat_16
#define GUI_FONT_28 lv_font_montserrat_18
#define GUI_FONT_36 lv_font_montserrat_24
#define GUI_FONT_48 lv_font_montserrat_30
#else
#define GUI_FONT_16 lv_font_montserrat_16
#define GUI_FONT_20 lv_font_montserrat_20
#define GUI_FONT_24 lv_font_montserrat_24
#define GUI_FONT_28 lv_font_montserrat_28
#define GUI_FONT_36 lv_font_montserrat_36
#define GUI_FONT_48 lv_font_montserrat_48
#endif
