#pragma once

#include "kline_task.h"

/* Builds the UI. Call with the LVGL lock held. */
void gui_build(void);

/* K-line task callback: updates widgets (takes the LVGL lock itself). */
void gui_on_kline_event(const kl_event_t *ev);

/* Debug console command such as "demo eurovan", "open 0", "faults" (takes the lock). */
void gui_debug(const char *cmd);
