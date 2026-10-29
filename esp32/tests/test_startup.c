/* Host regression test for real app_main startup and fallback callbacks. */
#include <assert.h>
#include <stdio.h>
#include "../main/main.c"

const char labels_csv_start[] asm("_binary_labels_csv_start") = "";
const char labels_csv_end[] asm("_binary_labels_csv_end") = "";

static bool display_ready, lock_ready;
static int gui_builds, console_starts, requests, unlocks;
static kl_callback_t callback;

int nvs_flash_init(void) { return ESP_OK; }
int nvs_flash_erase(void) { return ESP_OK; }
bool board_display_init(void) { return display_ready; }
bool board_lvgl_lock(void) { return lock_ready; }
void board_lvgl_unlock(void) { unlocks++; }
void gui_build(void) { gui_builds++; }
void gui_on_kline_event(const kl_event_t *event) { (void)event; }
void debug_console_start(void) { console_starts++; }
void kline_start(kl_callback_t cb) { callback = cb; }
bool kline_submit(const kl_request_t *request)
{
    (void)request;
    requests++;
    return true;
}

static void check_startup(bool display, bool lock)
{
    display_ready = display;
    lock_ready = lock;
    gui_builds = console_starts = requests = unlocks = 0;
    callback = NULL;
    app_main();
    assert(callback != NULL);
    assert(requests == 0);
    if (display && lock) {
        assert(gui_builds == 1 && console_starts == 1 && unlocks == 1);
        assert(callback == gui_on_kline_event);
        return;
    }
    assert(gui_builds == 0 && console_starts == 0 && unlocks == 0);
    /* Attach, completion and reconnect must never enqueue vehicle operations. */
    callback(&(kl_event_t){ .type = KL_EV_CABLE, .ok = true });
    callback(&(kl_event_t){ .type = KL_EV_DONE, .op = KL_SCAN });
    callback(&(kl_event_t){ .type = KL_EV_CABLE, .ok = false });
    callback(&(kl_event_t){ .type = KL_EV_CABLE, .ok = true });
    assert(requests == 0);
}

int main(void)
{
    check_startup(false, false);
    check_startup(true, false);
    check_startup(true, true);
    puts("PASS startup: display failure, UI lock failure, normal startup; no automatic requests");
    return 0;
}
