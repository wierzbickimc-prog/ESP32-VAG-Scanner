#pragma once

#include <stdbool.h>

/* Brings up the panel, touch and LVGL. Returns false if the display isn't available. */
bool board_display_init(void);

/* LVGL is not thread-safe: hold this lock for any lv_* call outside LVGL callbacks. */
bool board_lvgl_lock(void);
void board_lvgl_unlock(void);
