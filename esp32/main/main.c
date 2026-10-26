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

/*
 * Headless mode (no display driver yet): once the cable attaches, scan, then
 * read the engine's fault codes, logging everything to the serial monitor.
 */
static void headless_event(const kl_event_t *ev)
{
    switch (ev->type) {
    case KL_EV_CABLE:
        ESP_LOGI(TAG, "cable %s", ev->ok ? "ready" : "gone");
        if (ev->ok) {
            kl_request_t scan = { .op = KL_SCAN };
            kline_submit(&scan);
        }
        break;
    case KL_EV_PROGRESS:
        ESP_LOGI(TAG, "probing %02X", ev->address);
        break;
    case KL_EV_MODULE:
        ESP_LOGI(TAG, "found %02X (KW%d):\n%s", ev->address, ev->protocol, ev->ident);
        break;
    case KL_EV_DONE:
        if (ev->op == KL_SCAN) {
            ESP_LOGI(TAG, "scan done; reading engine faults");
            kl_request_t faults = { .op = KL_FAULTS, .address = 0x01 };
            kline_submit(&faults);
        }
        break;
    case KL_EV_FAULTS:
        if (!ev->ok) {
            ESP_LOGW(TAG, "faults %02X: %s", ev->address, kw_strerror(ev->err));
            break;
        }
        ESP_LOGI(TAG, "%d fault code(s) on %02X", ev->count, ev->address);
        for (int i = 0; i < ev->count; i++) {
            char line[24];
            const char *desc = kw_fault_describe(ev->faults[i].dtc);
            kw_format_fault(&ev->faults[i], line, sizeof(line));
            ESP_LOGI(TAG, "  %s  %s", line, desc ? desc : "");
        }
        break;
    case KL_EV_GROUP:
        break;
    }
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

    if (board_display_init()) {
        if (board_lvgl_lock()) {
            gui_build();
            board_lvgl_unlock();
        }
        kline_start(gui_on_kline_event);
        debug_console_start();
    } else {
        kline_start(headless_event);
    }
}
