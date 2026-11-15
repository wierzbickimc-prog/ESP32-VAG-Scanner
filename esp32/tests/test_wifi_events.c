#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_event.h"
#include "esp_wifi.h"

esp_event_base_t WIFI_EVENT = "wifi";
static bool added;
static int starts, stops, other;
static int station, payload;

void __wrap_esp_netif_action_start(void *, esp_event_base_t, int32_t, void *);
void __wrap_esp_netif_action_stop(void *, esp_event_base_t, int32_t, void *);

void __real_esp_netif_action_start(void *netif, esp_event_base_t base, int32_t id, void *data)
{
    assert(data == &payload);
    if (base != WIFI_EVENT || id != WIFI_EVENT_STA_START) {
        other++;
        return;
    }
    assert(netif == &station);
    /* Reproduces the lwIP assertion seen on the physical C6's second START. */
    assert(!added);
    added = true;
    starts++;
}

void __real_esp_netif_action_stop(void *netif, esp_event_base_t base, int32_t id, void *data)
{
    assert(data == &payload);
    if (base != WIFI_EVENT || id != WIFI_EVENT_STA_STOP) {
        other++;
        return;
    }
    assert(netif == &station && added);
    added = false;
    stops++;
}

int main(void)
{
    /* A stale STOP, duplicate START/STOP, then a legitimate restart. */
    __wrap_esp_netif_action_stop(&station, WIFI_EVENT, WIFI_EVENT_STA_STOP, &payload);
    __wrap_esp_netif_action_start(&station, WIFI_EVENT, WIFI_EVENT_STA_START, &payload);
    __wrap_esp_netif_action_start(&station, WIFI_EVENT, WIFI_EVENT_STA_START, &payload);
    assert(added && starts == 1);
    __wrap_esp_netif_action_stop(&station, WIFI_EVENT, WIFI_EVENT_STA_STOP, &payload);
    __wrap_esp_netif_action_stop(&station, WIFI_EVENT, WIFI_EVENT_STA_STOP, &payload);
    assert(!added && stops == 1);
    __wrap_esp_netif_action_start(&station, WIFI_EVENT, WIFI_EVENT_STA_START, &payload);
    assert(added && starts == 2);
    __wrap_esp_netif_action_start(NULL, "other", WIFI_EVENT_STA_START, &payload);
    __wrap_esp_netif_action_stop(NULL, "other", WIFI_EVENT_STA_STOP, &payload);
    assert(other == 2 && added);
    puts("PASS Wi-Fi duplicate START/STOP, restart, and unrelated event forwarding");
}
