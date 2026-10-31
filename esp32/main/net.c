#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "fault_table.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "wikitext.h"

static const char *TAG = "net";

#define RAW_MAX  (48 * 1024)   /* the longest fault code pages are a few kB */
#define TEXT_MAX (24 * 1024)
#define RETRY_MS 5000
#define AUTH_FAILS_MAX 4

typedef enum { OP_SCAN, OP_CONNECT, OP_FORGET, OP_WIKI } op_t;

typedef struct {
    op_t op;
    int dtc;
    char ssid[33];
    char pass[65];
} net_req_t;

static net_callback_t s_callback;
static QueueHandle_t s_queue;
static esp_timer_handle_t s_retry;
static volatile net_state_t s_state = NET_STARTING;
static char s_ssid[33], s_pass[65];
static char s_detail[48];
static volatile bool s_want;      /* a network is saved and we should stay connected */
static int s_auth_fails;

static void emit_state(net_state_t state, const char *detail)
{
    s_state = state;
    snprintf(s_detail, sizeof(s_detail), "%s", detail ? detail : "");
    if (s_callback)
        s_callback(&(net_event_t){ .type = NET_EV_STATE, .state = state, .ssid = s_ssid, .detail = s_detail });
}

net_state_t net_state(void)
{
    return s_state;
}

const char *net_saved_ssid(void)
{
    return s_ssid;
}

/* ---- saved network -------------------------------------------------------- */

static void load_saved(void)
{
    nvs_handle_t h;
    size_t n;
    s_ssid[0] = s_pass[0] = '\0';
    if (nvs_open("vagscanner", NVS_READONLY, &h) != ESP_OK)
        return;
    n = sizeof(s_ssid);
    if (nvs_get_str(h, "wifi_ssid", s_ssid, &n) != ESP_OK)
        s_ssid[0] = '\0';
    n = sizeof(s_pass);
    if (nvs_get_str(h, "wifi_pass", s_pass, &n) != ESP_OK)
        s_pass[0] = '\0';
    nvs_close(h);
}

static void save(void)
{
    nvs_handle_t h;
    if (nvs_open("vagscanner", NVS_READWRITE, &h) != ESP_OK)
        return;
    if (s_ssid[0]) {
        nvs_set_str(h, "wifi_ssid", s_ssid);
        nvs_set_str(h, "wifi_pass", s_pass);
    } else {
        nvs_erase_key(h, "wifi_ssid");
        nvs_erase_key(h, "wifi_pass");
    }
    nvs_commit(h);
    nvs_close(h);
}

/* ---- connection ----------------------------------------------------------- */

static void connect_saved(void)
{
    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, s_ssid, strlen(s_ssid));
    memcpy(cfg.sta.password, s_pass, strlen(s_pass));
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    s_want = true;
    s_auth_fails = 0;
    emit_state(NET_CONNECTING, "Connecting...");
    esp_wifi_connect();
}

static void on_retry(void *arg)
{
    (void)arg;
    if (s_want)
        esp_wifi_connect();
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_want)
            return;
        const wifi_event_sta_disconnected_t *d = data;
        bool auth = d->reason == WIFI_REASON_AUTH_FAIL || d->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
                    || d->reason == WIFI_REASON_HANDSHAKE_TIMEOUT || d->reason == WIFI_REASON_AUTH_EXPIRE;
        ESP_LOGI(TAG, "disconnected, reason %d", d->reason);
        if (auth && ++s_auth_fails >= AUTH_FAILS_MAX) {
            s_want = false;
            emit_state(NET_FAILED, "Wrong password?");
            return;
        }
        emit_state(NET_CONNECTING, d->reason == WIFI_REASON_NO_AP_FOUND ? "Network not found, retrying..."
                                                                         : "Retrying...");
        esp_timer_start_once(s_retry, RETRY_MS * 1000ull);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        char ip[20];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_auth_fails = 0;
        emit_state(NET_CONNECTED, ip);
    }
}

/* ---- scan ----------------------------------------------------------------- */

static int by_rssi(const void *a, const void *b)
{
    return ((const net_ap_t *)b)->rssi - ((const net_ap_t *)a)->rssi;
}

static void do_scan(void)
{
    static wifi_ap_record_t recs[30];
    static net_ap_t aps[20];
    uint16_t n = 30;
    int count = 0;

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err == ESP_OK)
        err = esp_wifi_scan_get_ap_records(&n, recs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        s_callback(&(net_event_t){ .type = NET_EV_SCAN, .count = -1 });
        return;
    }
    for (int i = 0; i < n && count < 20; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        bool dup = false;
        for (int j = 0; j < count; j++)
            dup |= strcmp(aps[j].ssid, ssid) == 0;
        if (!ssid[0] || dup)
            continue;
        snprintf(aps[count].ssid, sizeof(aps[count].ssid), "%s", ssid);
        aps[count].rssi = recs[i].rssi;
        aps[count].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        count++;
    }
    qsort(aps, count, sizeof(aps[0]), by_rssi);
    s_callback(&(net_event_t){ .type = NET_EV_SCAN, .count = count, .aps = aps });
}

/* ---- wiki pages ----------------------------------------------------------- */

typedef struct {
    char *buf;
    int len;
} Body;

static esp_err_t on_http(esp_http_client_event_t *e)
{
    Body *b = e->user_data;
    if (e->event_id == HTTP_EVENT_ON_DATA && b->len + e->data_len < RAW_MAX) {
        memcpy(b->buf + b->len, e->data, e->data_len);
        b->len += e->data_len;
    }
    return ESP_OK;
}

/* GETs the page source. Returns the HTTP status, or -1 on a connection error. */
static int get_raw(const char *title, Body *body)
{
    char url[160];
    snprintf(url, sizeof(url), "https://wiki.ross-tech.com/wiki/index.php?title=%s&action=raw", title);
    body->len = 0;
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .user_agent = "ESP32-VAG-Scanner/1.0 (fault code viewer)",
        .event_handler = on_http,
        .user_data = body,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c)
        return -1;
    esp_err_t err = esp_http_client_perform(c);
    int status = err == ESP_OK ? esp_http_client_get_status_code(c) : -1;
    if (err != ESP_OK)
        ESP_LOGW(TAG, "GET %s: %s", title, esp_err_to_name(err));
    esp_http_client_cleanup(c);
    body->buf[body->len] = '\0';
    return status;
}

static void do_wiki(int dtc)
{
    static char *raw, *text;
    char url[96], title[96];

    if (!raw) {
        raw = heap_caps_malloc(RAW_MAX + 1, MALLOC_CAP_SPIRAM);
        text = heap_caps_malloc(TEXT_MAX, MALLOC_CAP_SPIRAM);
    }
    net_event_t ev = { .type = NET_EV_PAGE, .dtc = dtc };
    if (!raw || !text) {
        ev.text = "Out of memory.";
    } else if (s_state != NET_CONNECTED) {
        ev.text = "Not connected to Wi-Fi. Connect on the Wi-Fi screen, or scan the QR code.";
    } else {
        kw_fault_wiki_url(dtc, url, sizeof(url));
        const char *page = strstr(url, "index.php/");
        snprintf(title, sizeof(title), "%s", page ? page + 10 : url);

        Body body = { raw, 0 };
        int status = get_raw(title, &body);
        if (status == 200 && kw_wikitext_redirect(raw, title, sizeof(title)))
            status = get_raw(title, &body);
        if (status == 200 && body.len > 0) {
            kw_wikitext_render(raw, body.len, text, TEXT_MAX);
            ev.ok = text[0] != '\0';
            ev.text = ev.ok ? text : "The Ross-Tech page for this code is empty.";
        } else if (status == 404) {
            ev.text = "Ross-Tech has no page for this code.";
        } else if (status < 0) {
            ev.text = "Couldn't reach wiki.ross-tech.com. Check the Wi-Fi connection.";
        } else {
            snprintf(text, TEXT_MAX, "The wiki answered with HTTP %d.", status);
            ev.text = text;
        }
    }
    s_callback(&ev);
}

/* ---- task ----------------------------------------------------------------- */

static bool wifi_up(void)
{
    esp_netif_init();
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        return false;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&cfg)) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi co-processor unavailable: %s", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* we keep the network in our own NVS keys */
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, on_wifi_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);
    return esp_wifi_start() == ESP_OK;
}

static void net_task(void *arg)
{
    (void)arg;
    const esp_timer_create_args_t retry = { .callback = on_retry, .name = "wifi_retry" };
    esp_timer_create(&retry, &s_retry);

    if (!wifi_up()) {
        emit_state(NET_UNAVAILABLE, "Wi-Fi unavailable");
        vTaskDelete(NULL);
        return;
    }
    load_saved();
    if (s_ssid[0])
        connect_saved();
    else
        emit_state(NET_OFF, "Not set up");

    for (;;) {
        net_req_t req;
        if (xQueueReceive(s_queue, &req, portMAX_DELAY) != pdTRUE)
            continue;
        switch (req.op) {
        case OP_SCAN:
            do_scan();
            break;
        case OP_CONNECT:
            esp_timer_stop(s_retry);
            snprintf(s_ssid, sizeof(s_ssid), "%s", req.ssid);
            snprintf(s_pass, sizeof(s_pass), "%s", req.pass);
            save();
            connect_saved();
            break;
        case OP_FORGET:
            s_want = false;
            esp_timer_stop(s_retry);
            esp_wifi_disconnect();
            s_ssid[0] = s_pass[0] = '\0';
            save();
            emit_state(NET_OFF, "Not set up");
            break;
        case OP_WIKI:
            do_wiki(req.dtc);
            break;
        }
    }
}

void net_start(net_callback_t callback)
{
    s_callback = callback;
    s_queue = xQueueCreate(4, sizeof(net_req_t));
    /* Below the K-line task (6) so diagnostics keep their timing. TLS needs a deep stack. */
    xTaskCreate(net_task, "net", 12288, NULL, 4, NULL);
}

static void submit(const net_req_t *req)
{
    if (s_queue && s_state != NET_UNAVAILABLE && s_state != NET_STARTING)
        xQueueSend(s_queue, req, 0);
    else if (req->op == OP_WIKI && s_callback)
        s_callback(&(net_event_t){ .type = NET_EV_PAGE, .dtc = req->dtc,
                                   .text = "Wi-Fi isn't available on this unit." });
    else if (req->op == OP_SCAN && s_callback)
        s_callback(&(net_event_t){ .type = NET_EV_SCAN, .count = -1 });
}

void net_scan(void)
{
    submit(&(net_req_t){ .op = OP_SCAN });
}

void net_connect(const char *ssid, const char *password)
{
    net_req_t req = { .op = OP_CONNECT };
    snprintf(req.ssid, sizeof(req.ssid), "%s", ssid);
    snprintf(req.pass, sizeof(req.pass), "%s", password ? password : "");
    submit(&req);
}

void net_forget(void)
{
    submit(&(net_req_t){ .op = OP_FORGET });
}

void net_fetch_wiki(int dtc)
{
    submit(&(net_req_t){ .op = OP_WIKI, .dtc = dtc });
}
