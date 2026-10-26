/*
 * Serial debug console on the USB-serial port, so the UI can be driven and
 * captured from the Mac (assets/tools/p4shot.py):
 *   shot              half-size screenshot: "SHOT w h stride", base64 rows, "ENDSHOT"
 *   demo <car>        scan a simulated car (eurovan / harlequin / golf)
 *   open <n>          open the n-th controller
 *   faults | live     read faults / toggle live data on the open controller
 *   plus | minus      step the measuring group
 *   back | home | credits | chooser
 */
#include "debug_console.h"

#include <stdio.h>
#include <string.h>
#include "board_display.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui.h"
#include "lvgl.h"

static void base64_line(const uint8_t *in, size_t n)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[80];
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? tbl[v & 63] : '=';
    }
    out[o++] = '\n';
    fwrite(out, 1, o, stdout);
}

static void send_shot(void)
{
    if (!board_lvgl_lock())
        return;
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    board_lvgl_unlock();
    if (!buf) {
        printf("SHOT ERR\n");
        return;
    }

    esp_log_level_set("*", ESP_LOG_NONE);
    /* Half resolution (2x2 average): the console runs at 115200, so this keeps a shot near a minute. */
    const lv_image_header_t *h = &buf->header;
    const int w2 = h->w / 2, h2 = h->h / 2;
    printf("\nSHOT %d %d %d\n", w2, h2, w2 * 2);
    static uint8_t line[800 * 2];
    uint8_t pending[57];
    size_t npend = 0;
    for (int y = 0; y < h2; y++) {
        const uint16_t *r0 = (const uint16_t *)(buf->data + (size_t)(2 * y) * h->stride);
        const uint16_t *r1 = (const uint16_t *)(buf->data + (size_t)(2 * y + 1) * h->stride);
        for (int x = 0; x < w2; x++) {
            uint16_t p[4] = { r0[2 * x], r0[2 * x + 1], r1[2 * x], r1[2 * x + 1] };
            int r = 0, g = 0, b = 0;
            for (int i = 0; i < 4; i++) {
                r += p[i] >> 11;
                g += (p[i] >> 5) & 0x3F;
                b += p[i] & 0x1F;
            }
            uint16_t v = (uint16_t)((r / 4) << 11 | (g / 4) << 5 | (b / 4));
            line[2 * x] = v & 0xFF;
            line[2 * x + 1] = v >> 8;
        }
        for (int i = 0; i < w2 * 2; i++) {
            pending[npend++] = line[i];
            if (npend == sizeof(pending)) {
                base64_line(pending, npend);
                npend = 0;
            }
        }
    }
    if (npend)
        base64_line(pending, npend);
    printf("ENDSHOT\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);

    if (board_lvgl_lock()) {
        lv_draw_buf_destroy(buf);
        board_lvgl_unlock();
    }
}

static void console_task(void *arg)
{
    (void)arg;
    const int port = CONFIG_ESP_CONSOLE_UART_NUM;
    char line[64];
    size_t len = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(port, &c, 1, portMAX_DELAY) != 1)
            continue;
        if (c != '\r' && c != '\n') {
            /* skip line noise (the board reset glitches the first byte) */
            if (c >= 0x20 && c < 0x7F && len < sizeof(line) - 1)
                line[len++] = (char)c;
            continue;
        }
        line[len] = '\0';
        len = 0;
        if (!line[0])
            continue;
        ESP_LOGI("console", "> %s", line);
        if (strcmp(line, "shot") == 0)
            send_shot();
        else
            gui_debug(line);
    }
}

void debug_console_start(void)
{
    /* Install the driver for reads; console output keeps working through it. */
    const int port = CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(port)) {
        esp_err_t err = uart_driver_install(port, 1024, 0, 0, NULL, 0);
        if (err != ESP_OK) {
            ESP_LOGE("console", "UART%d driver: %s", port, esp_err_to_name(err));
            return;
        }
    }
    uart_vfs_dev_use_driver(port);
    /* lv_snapshot_take renders on this task's stack: give it room */
    xTaskCreate(console_task, "console", 16384, NULL, 2, NULL);
    ESP_LOGI("console", "debug console on UART%d", port);
}
