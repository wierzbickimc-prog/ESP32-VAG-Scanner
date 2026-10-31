#pragma once

#include <stdbool.h>

/*
 * Wi-Fi through the board's ESP32-C6 (ESP-Hosted over SDIO), used only to
 * fetch Ross-Tech wiki pages for fault codes. One task owns it; the UI
 * submits requests and gets events through the callback (called from that
 * task or the event loop: take the LVGL lock before touching widgets).
 * Nothing here touches the K-line, and a missing C6 only disables Wi-Fi.
 */

typedef enum {
    NET_STARTING,
    NET_UNAVAILABLE,   /* the C6 didn't answer */
    NET_OFF,           /* no network saved */
    NET_CONNECTING,
    NET_CONNECTED,
    NET_FAILED,        /* gave up: wrong password or network gone */
} net_state_t;

typedef struct {
    char ssid[33];
    int rssi;
    bool secure;
} net_ap_t;

typedef enum {
    NET_EV_STATE,      /* `state`, `ssid`, `detail` (IP address or reason) */
    NET_EV_SCAN,       /* `count` networks in `aps`, strongest first; count < 0 on error */
    NET_EV_PAGE,       /* wiki page for `dtc`: `ok` and `text` (see wikitext.h), or `text` = error */
} net_ev_type_t;

typedef struct {
    net_ev_type_t type;
    net_state_t state;
    const char *ssid;
    const char *detail;
    int count;
    const net_ap_t *aps;
    int dtc;
    bool ok;
    const char *text;
} net_event_t;

typedef void (*net_callback_t)(const net_event_t *ev);

/* Starts the Wi-Fi task; reconnects to the saved network if there is one. */
void net_start(net_callback_t callback);

net_state_t net_state(void);

/* Name of the saved network, or "" if none. */
const char *net_saved_ssid(void);

void net_scan(void);

/* Saves the network and connects. */
void net_connect(const char *ssid, const char *password);

/* Disconnects and forgets the saved network. */
void net_forget(void);

/* Fetches the Ross-Tech wiki page for a fault code. */
void net_fetch_wiki(int dtc);
