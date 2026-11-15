/* Some factory ESP-Hosted C6 images send STA_START twice. ESP-IDF 5.5's
 * default handler adds the same lwIP netif twice and asserts. These wrappers
 * make the start/stop lifecycle idempotent for our single Wi-Fi station.
 * Both callbacks run on the default event loop, which serializes this state.
 */
#include <stddef.h>
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_log.h"

static void *s_started_station;

void __real_esp_netif_action_start(void *netif, esp_event_base_t base, int32_t id, void *data);
void __real_esp_netif_action_stop(void *netif, esp_event_base_t base, int32_t id, void *data);

void __wrap_esp_netif_action_start(void *netif, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START && netif) {
        if (s_started_station == netif) {
            ESP_LOGW("net", "ignored duplicate Wi-Fi station start");
            return;
        }
        s_started_station = netif;
    }
    __real_esp_netif_action_start(netif, base, id, data);
}

void __wrap_esp_netif_action_stop(void *netif, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP && netif) {
        if (s_started_station != netif)
            return;
        s_started_station = NULL;
    }
    __real_esp_netif_action_stop(netif, base, id, data);
}
