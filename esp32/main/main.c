#include <stdio.h>
#include "board_display.h"
#include "debug_console.h"
#include "fault_table.h"
#include "labels.h"
#include "esp_log.h"
#include "gui.h"
#include "kline_task.h"
#include "nvs_flash.h"

static const char *TAG = "vagscanner";

extern const char labels_csv_start[] asm("_binary_labels_csv_start");
extern const char labels_csv_end[] asm("_binary_labels_csv_end");

#ifdef HAVE_FAULT_TABLE
extern const char fault_csv_start[] asm("_binary_ross_tech_fault_codes_csv_start");
extern const char fault_csv_end[] asm("_binary_ross_tech_fault_codes_csv_end");
#endif

static void load_fault_table(void)
{
#ifdef HAVE_FAULT_TABLE
    /* EMBED_TXTFILES appends a NUL; don't count it. */
    int n = kw_fault_table_load(fault_csv_start, fault_csv_end - fault_csv_start - 1);
    ESP_LOGI(TAG, "fault code table: %d descriptions", n);
#else
    ESP_LOGI(TAG, "no fault code table built in");
#endif
}

/* A missing display must never initiate vehicle communication. */
static void headless_event(const kl_event_t *ev)
{
    if (ev->type == KL_EV_CABLE)
        ESP_LOGI(TAG, "cable %s; no vehicle communication requested", ev->ok ? "ready" : "gone");
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 VAG Scanner starting");
    if (nvs_flash_init() != ESP_OK) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    load_fault_table();
    ESP_LOGI(TAG, "measuring-block labels: %d", kw_labels_load(labels_csv_start, labels_csv_end - labels_csv_start - 1));

    if (board_display_init() && board_lvgl_lock()) {
        gui_build();
        board_lvgl_unlock();
        kline_start(gui_on_kline_event);
        debug_console_start();
    } else {
        ESP_LOGW(TAG, "Display unavailable; vehicle communication disabled until the UI is restored");
        kline_start(headless_event);
    }
}
