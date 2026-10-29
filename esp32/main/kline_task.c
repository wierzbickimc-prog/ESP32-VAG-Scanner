#include "kline_task.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "io_esp32_ftdi.h"
#include "labels.h"

static const char *TAG = "kline";

#define BAUD 9600

static const uint8_t SCAN_ADDRESSES[] = {
    0x01, 0x02, 0x03, 0x08, 0x09, 0x15, 0x16, 0x17, 0x19, 0x25,
    0x35, 0x36, 0x37, 0x45, 0x46, 0x55, 0x56, 0x57,
};
#define SCAN_COUNT (int)(sizeof(SCAN_ADDRESSES) / sizeof(SCAN_ADDRESSES[0]))

static QueueHandle_t s_queue;
static kl_callback_t s_callback;
static volatile bool s_cancel;
static KwIo s_cable_io;
static KwSession s_session;

/* demo car */
static bool s_demo;
static FakeEcu s_fake;
static KwIo s_fake_io;

static void emit(kl_event_t ev)
{
    ev.demo = s_demo;
    if (s_callback)
        s_callback(&ev);
}

static KwIo *bus(void)
{
    return s_demo ? &s_fake_io : &s_cable_io;
}

/* The simulated car answers instantly; pace it like a real bus. */
static void demo_pause(int ms)
{
    if (s_demo)
        vTaskDelay(pdMS_TO_TICKS(ms));
}

static int connect(uint8_t address, int attempts)
{
    demo_pause(350);
    return kw_connect_attempts(&s_session, bus(), address, attempts);
}

static void do_scan(void)
{
    s_cancel = false;
    for (int i = 0; i < SCAN_COUNT && !s_cancel; i++) {
        uint8_t addr = SCAN_ADDRESSES[i];
        emit((kl_event_t){ .type = KL_EV_PROGRESS, .address = addr, .count = i, .total = SCAN_COUNT });
        demo_pause(120);
        int err = kw_connect_attempts(&s_session, bus(), addr, 1);
        if (err == KW_OK) {
            emit((kl_event_t){ .type = KL_EV_MODULE, .ok = true, .address = addr,
                               .protocol = s_session.protocol, .ident = s_session.ident });
            if (s_demo) {
                /* demos show fault counts straight away */
                static FaultCode codes[64];
                int n = kw_read_fault_codes(&s_session, codes, 64);
                emit((kl_event_t){ .type = KL_EV_FAULTS, .op = KL_FAULTS, .address = addr, .ok = n >= 0,
                                   .err = n < 0 ? n : 0, .count = n < 0 ? 0 : n, .faults = codes });
            }
            kw_end(&s_session);
        } else if (err == KW_EKWP2000) {
            emit((kl_event_t){ .type = KL_EV_MODULE, .ok = false, .address = addr,
                               .protocol = s_session.protocol, .ident = "" });
        }
    }
}

static void do_faults(const kl_request_t *req)
{
    static FaultCode codes[64];
    int n = connect(req->address, 3);
    if (n == KW_OK || n == KW_EKWP2000) {
        emit((kl_event_t){ .type = KL_EV_MODULE, .op = req->op, .ok = n == KW_OK,
                           .address = req->address, .protocol = s_session.protocol,
                           .ident = s_session.ident });
    }
    if (n == KW_OK) {
        n = req->op == KL_CLEAR ? kw_clear_fault_codes(&s_session, codes, 64)
                                : kw_read_fault_codes(&s_session, codes, 64);
        kw_end(&s_session);
    }
    emit((kl_event_t){ .type = KL_EV_FAULTS, .op = req->op, .address = req->address, .ok = n >= 0,
                       .err = n < 0 ? n : 0, .count = n < 0 ? 0 : n, .faults = codes });
}

static void do_group(const kl_request_t *req)
{
    static char text[16][32];
    static const char *labels[16];
    SensorValue values[16];
    int n = connect(req->address, 3);
    if (n != KW_OK) {
        emit((kl_event_t){ .type = KL_EV_GROUP, .address = req->address, .err = n });
        return;
    }
    s_cancel = false;
    do {
        n = kw_group_read(&s_session, req->group, values, 16);
        for (int i = 0; i < n; i++) {
            kw_format_sensor(&values[i], text[i], sizeof(text[i]));
            labels[i] = kw_label(s_session.ident, req->group, i + 1, values[i].sensor_id);
        }
        emit((kl_event_t){ .type = KL_EV_GROUP, .address = req->address, .ok = n >= 0,
                           .err = n < 0 ? n : 0, .count = n < 0 ? 0 : n, .values = text,
                           .labels = labels });
        if (req->op == KL_GROUP_LIVE && n >= 0)
            vTaskDelay(pdMS_TO_TICKS(s_demo ? 500 : 150));
    } while (req->op == KL_GROUP_LIVE && n >= 0 && !s_cancel);
    kw_end(&s_session);
}

/* Opens the cable when it appears and reports changes. */
static void poll_cable(bool *was_connected)
{
    if (ftdi_connected())
        return;
    if (*was_connected) {
        ftdi_close();
        *was_connected = false;
        emit((kl_event_t){ .type = KL_EV_CABLE, .ok = false });
    }
    if (ftdi_open(BAUD, 200)) {
        kw_reset_interface(&s_cable_io);
        *was_connected = true;
        emit((kl_event_t){ .type = KL_EV_CABLE, .ok = true });
    }
}

static void kline_task(void *arg)
{
    (void)arg;
    bool was_connected = false;
    ftdi_attach(&s_cable_io);

    for (;;) {
        if (!s_demo)
            poll_cable(&was_connected);

        kl_request_t req;
        if (xQueueReceive(s_queue, &req, pdMS_TO_TICKS(300)) != pdTRUE)
            continue;
        ESP_LOGI(TAG, "op %d address %02X%s", req.op, req.address, s_demo ? " (demo)" : "");

        if (req.op == KL_DEMO) {
            s_demo = true;
            fake_ecu_init_car(&s_fake, &s_fake_io, req.demo_car);
            s_fake.live = 1;
            emit((kl_event_t){ .type = KL_EV_CABLE, .ok = true });
            do_scan();
        } else if (req.op == KL_SCAN) {
            if (s_demo) {
                s_demo = false;
                emit((kl_event_t){ .type = KL_EV_CABLE, .ok = ftdi_connected() });
            }
            if (ftdi_connected())
                do_scan();
        } else if (!s_demo && !ftdi_connected()) {
            /* no cable: answer with an error so the UI isn't left waiting */
            if (req.op == KL_FAULTS || req.op == KL_CLEAR)
                emit((kl_event_t){ .type = KL_EV_FAULTS, .op = req.op, .address = req.address, .err = KW_ENOWAKE });
            else
                emit((kl_event_t){ .type = KL_EV_GROUP, .address = req.address, .err = KW_ENOWAKE });
        } else if (req.op == KL_FAULTS || req.op == KL_CLEAR) {
            do_faults(&req);
        } else {
            do_group(&req);
        }
        emit((kl_event_t){ .type = KL_EV_DONE, .op = req.op });
    }
}

void kline_start(kl_callback_t callback)
{
    s_callback = callback;
    s_queue = xQueueCreate(4, sizeof(kl_request_t));
    ftdi_usb_start();
    /* Above LVGL so K-line timing wins; the task blocks between bytes. */
    xTaskCreate(kline_task, "kline", 8192, NULL, 6, NULL);
}

bool kline_submit(const kl_request_t *request)
{
    return xQueueSend(s_queue, request, 0) == pdTRUE;
}

void kline_cancel(void)
{
    s_cancel = true;
}
