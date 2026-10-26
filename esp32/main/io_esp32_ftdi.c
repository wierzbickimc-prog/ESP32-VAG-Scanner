/*
 * KwIo backend for an FTDI KKL cable plugged into the P4's USB host port.
 *
 * The cable is driven with the CDC-ACM host driver's raw bulk transfers plus
 * FTDI vendor control requests (the same ones libftdi sends):
 *   0x00 reset/purge, 0x01 modem control (DTR), 0x02 flow control,
 *   0x03 baud divisor, 0x04 line settings incl. BREAK (bit 14), 0x09 latency.
 * Every bulk-IN packet starts with 2 modem-status bytes, stripped here.
 */
#include "io_esp32_ftdi.h"

#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

static const char *TAG = "ftdi";

#define FTDI_VID 0x0403
#define FTDI_PID 0x6001
#define FTDI_PACKET 64          /* full-speed bulk max packet size */
#define ECHO_TIMEOUT_MS 50

#define REQ_OUT 0x40            /* vendor, host-to-device, device recipient */
#define SIO_RESET 0x00
#define SIO_MODEM_CTRL 0x01
#define SIO_SET_FLOW_CTRL 0x02
#define SIO_SET_BAUD_RATE 0x03
#define SIO_SET_DATA 0x04
#define SIO_SET_LATENCY 0x09

#define DATA_8N1 0x0008
#define DATA_BREAK (1 << 14)
#define FTDI_INDEX 1            /* interface A */

static cdc_acm_dev_hdl_t s_dev;
static volatile bool s_connected;
static StreamBufferHandle_t s_rx;

static void usb_host_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            usb_host_device_free_all();
    }
}

static bool on_rx(const uint8_t *data, size_t len, void *arg)
{
    (void)arg;
    /* Each 64-byte packet carries a 2-byte modem status header. */
    for (size_t off = 0; off < len; off += FTDI_PACKET) {
        size_t chunk = len - off < FTDI_PACKET ? len - off : FTDI_PACKET;
        if (chunk > 2)
            xStreamBufferSend(s_rx, data + off + 2, chunk - 2, 0);
    }
    return true;
}

static void on_event(const cdc_acm_host_dev_event_data_t *event, void *arg)
{
    (void)arg;
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        ESP_LOGW(TAG, "cable disconnected");
        s_connected = false;
    } else if (event->type == CDC_ACM_HOST_ERROR) {
        ESP_LOGE(TAG, "CDC-ACM error %d", event->data.error);
    }
}

void ftdi_usb_start(void)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    xTaskCreate(usb_host_task, "usb_host", 4096, NULL, 10, NULL);
    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));
    s_rx = xStreamBufferCreate(1024, 1);
}

static esp_err_t vendor(uint8_t request, uint16_t value, uint16_t index)
{
    return cdc_acm_host_send_custom_request(s_dev, REQ_OUT, request, value, index, 0, NULL);
}

/* FT232R/BM divisor: 3 MHz / baud in eighths, fraction encoded per FTDI AN232B-05. */
static void baud_divisor(int baud, uint16_t *value, uint16_t *index)
{
    static const uint8_t frac_code[8] = { 0, 3, 2, 4, 1, 5, 6, 7 };
    uint32_t eighths = (24000000u + (uint32_t)baud / 2) / (uint32_t)baud;
    uint32_t encoded = (eighths >> 3) | ((uint32_t)frac_code[eighths & 7] << 14);
    *value = (uint16_t)encoded;
    *index = (uint16_t)(encoded >> 16);
}

bool ftdi_open(int baud, int timeout_ms)
{
    if (s_connected)
        return true;

    const cdc_acm_host_device_config_t config = {
        .connection_timeout_ms = timeout_ms,
        .out_buffer_size = 64,
        .in_buffer_size = FTDI_PACKET,
        .event_cb = on_event,
        .data_cb = on_rx,
        .user_arg = NULL,
    };
    if (cdc_acm_host_open(FTDI_VID, FTDI_PID, 0, &config, &s_dev) != ESP_OK)
        return false;

    uint16_t div_value, div_index;
    baud_divisor(baud, &div_value, &div_index);

    esp_err_t err = ESP_OK;
    err |= vendor(SIO_RESET, 0, FTDI_INDEX);
    err |= vendor(SIO_SET_LATENCY, 1, FTDI_INDEX);
    err |= vendor(SIO_SET_FLOW_CTRL, 0, FTDI_INDEX);
    err |= vendor(SIO_SET_DATA, DATA_8N1, FTDI_INDEX);
    err |= vendor(SIO_SET_BAUD_RATE, div_value, div_index);
    err |= vendor(SIO_MODEM_CTRL, 0x0101, FTDI_INDEX); /* DTR on */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "configuring the cable failed");
        cdc_acm_host_close(s_dev);
        s_dev = NULL;
        return false;
    }

    xStreamBufferReset(s_rx);
    s_connected = true;
    ESP_LOGI(TAG, "KKL cable ready at %d baud", baud);
    return true;
}

bool ftdi_connected(void)
{
    return s_connected;
}

void ftdi_close(void)
{
    s_connected = false;
    if (s_dev) {
        cdc_acm_host_close(s_dev);
        s_dev = NULL;
    }
}

/* ---- KwIo ---------------------------------------------------------------- */

static int io_read(void *ctx, int timeout_ms)
{
    (void)ctx;
    uint8_t b;
    if (!s_connected)
        return -1;
    if (xStreamBufferReceive(s_rx, &b, 1, pdMS_TO_TICKS(timeout_ms)) != 1)
        return -1;
    return b;
}

static void io_write(void *ctx, uint8_t b)
{
    (void)ctx;
    uint8_t echo;
    if (!s_connected)
        return;
    cdc_acm_host_data_tx_blocking(s_dev, &b, 1, 100);
    /* Half-duplex: swallow our own byte. */
    xStreamBufferReceive(s_rx, &echo, 1, pdMS_TO_TICKS(ECHO_TIMEOUT_MS));
}

static void io_break(void *ctx, int on)
{
    (void)ctx;
    if (s_connected)
        vendor(SIO_SET_DATA, on ? (DATA_8N1 | DATA_BREAK) : DATA_8N1, FTDI_INDEX);
}

static void io_dtr(void *ctx, int on)
{
    (void)ctx;
    if (s_connected)
        vendor(SIO_MODEM_CTRL, on ? 0x0101 : 0x0100, FTDI_INDEX);
}

static void io_flush(void *ctx)
{
    (void)ctx;
    xStreamBufferReset(s_rx);
}

static uint64_t io_now(void *ctx)
{
    (void)ctx;
    return (uint64_t)esp_timer_get_time();
}

/* Block for the bulk of long waits, spin the last couple of milliseconds. */
static void io_sleep(void *ctx, uint32_t us)
{
    (void)ctx;
    int64_t end = esp_timer_get_time() + us;
    if (us > 3000)
        vTaskDelay(pdMS_TO_TICKS((us - 2000) / 1000));
    int64_t left = end - esp_timer_get_time();
    if (left > 0)
        esp_rom_delay_us((uint32_t)left);
}

void ftdi_attach(KwIo *io)
{
    io->read_byte = io_read;
    io->write_byte = io_write;
    io->set_break = io_break;
    io->set_dtr = io_dtr;
    io->flush_in = io_flush;
    io->now_us = io_now;
    io->sleep_us = io_sleep;
    io->ctx = NULL;
}
