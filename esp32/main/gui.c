/*
 * LVGL 9 UI for the 800x1280 portrait panel. Dark theme; the detected car's
 * photo fills the top of the home screen, controllers sit in a card grid
 * below, and each controller opens a detail screen with Faults / Live data.
 * The dashboard screen shows four live values with trends (kwcore/dashboard.c).
 * Wi-Fi (net.c) only feeds the Ross-Tech wiki viewer for fault codes.
 */
#include "gui.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "board_display.h"
#include "car_profile.h"
#include "esp_timer.h"
#include "fault_table.h"
#include "labels.h"
#include "lvgl.h"
#include "net.h"
#include "nvs.h"

#define C_BG       0x0D1014
#define C_SURFACE  0x161B22
#define C_SURFACE2 0x222A35
#define C_TEXT     0xE8ECF1
#define C_MUTED    0x8B95A3
#define C_ACCENT   0x3D8BFF
#define C_OK       0x3FB950
#define C_WARN     0xF5A524
#define C_ERR      0xFF5A5F

#define SCREEN_W 800
#define SCREEN_H 1280
#define MAX_MODULES 32
#define MAX_VALUES 8

typedef struct {
    uint8_t address;
    int protocol;
    bool supported;
    int faults; /* -1 until read */
    int error;
    char ident[KW_IDENT_MAX];
    lv_obj_t *status, *ident_label;
} module_t;

static module_t s_modules[MAX_MODULES];
static int s_module_count;
static const uint8_t LANDING_ADDRESSES[] = { 0x01, 0x02, 0x03, 0x08, 0x15, 0x17 };
static int s_current = -1;

static bool s_cable, s_demo, s_busy, s_scanning, s_live, s_scanned;
static bool s_restart_live;  /* group changed while live: restart on the new group */
static bool s_group_pending; /* group changed while busy: read it when free */
static KwCarVotes s_votes;
static KwCar s_car;
static bool s_car_manual;
static int s_demo_next;

/* home */
static lv_obj_t *s_home, *s_hero, *s_title, *s_subtitle;
static lv_obj_t *s_pill_dot, *s_pill_label;
static lv_obj_t *s_scan_label, *s_progress, *s_scan_status;
static lv_obj_t *s_count_label, *s_grid;
/* detail */
static lv_obj_t *s_detail, *s_detail_hero, *s_detail_name, *s_detail_sub, *s_detail_ident;
static lv_obj_t *s_tab_faults, *s_tab_live, *s_panel_faults, *s_panel_live;
static lv_obj_t *s_fault_status, *s_fault_list;
static lv_obj_t *s_group_label, *s_live_label, *s_group_status;
static lv_obj_t *s_value_tiles[MAX_VALUES], *s_value_labels[MAX_VALUES], *s_value_names[MAX_VALUES];
static int s_group = 1;
/* credits */
static lv_obj_t *s_credits;

/* ---- building blocks ------------------------------------------------------ */

static lv_color_t col(uint32_t hex)
{
    return lv_color_hex(hex);
}

static lv_obj_t *plain(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *str)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, col(color), 0);
    lv_label_set_text(l, str);
    return l;
}

static lv_obj_t *surface(lv_obj_t *parent, uint32_t color, int radius)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_style_bg_color(o, col(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    return o;
}

static lv_obj_t *button(lv_obj_t *parent, const char *label, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_style_bg_color(b, col(bg), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_mix(col(0xFFFFFF), col(bg), 40), LV_STATE_PRESSED);
    lv_obj_set_style_opa(b, LV_OPA_50, LV_STATE_DISABLED);
    if (label)
        lv_obj_center(text(b, &lv_font_montserrat_24, fg, label));
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    return b;
}

static lv_obj_t *round_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb)
{
    lv_obj_t *b = button(parent, NULL, 0x000000, C_TEXT, cb, NULL);
    lv_obj_set_size(b, 64, 64);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_60, 0);
    lv_obj_center(text(b, &lv_font_montserrat_24, C_TEXT, symbol));
    return b;
}

static lv_obj_t *column(lv_obj_t *parent, int gap)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_width(o, LV_PCT(100));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(o, gap, 0);
    return o;
}

static lv_obj_t *row(lv_obj_t *parent, int gap)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_width(o, LV_PCT(100));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, gap, 0);
    return o;
}

/* A gradient from transparent to the page colour, so photos melt into it. */
static void scrim(lv_obj_t *parent, int y, int h)
{
    lv_obj_t *o = plain(parent);
    lv_obj_set_size(o, SCREEN_W, h);
    lv_obj_set_pos(o, 0, y);
    lv_obj_set_style_bg_color(o, col(C_BG), 0);
    lv_obj_set_style_bg_grad_color(o, col(C_BG), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_main_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_grad_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
}

/* The built-in Montserrat has ASCII and LVGL symbols only: map the shared
 * profile strings' typography onto those. */
static const char *p4_text(const char *in)
{
    static char out[128];
    size_t o = 0;
    for (const char *p = in; *p && o < sizeof(out) - 4; p++) {
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xB7) {          /* middle dot */
            memcpy(out + o, LV_SYMBOL_BULLET, 3);
            o += 3;
            p += 1;
        } else if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x93) {
            out[o++] = '-';                                                          /* en dash */
            p += 2;
        } else {
            out[o++] = *p;
        }
    }
    out[o] = '\0';
    return out;
}

static const char *module_name(uint8_t address)
{
    const char *name = kw_module_name(address);
    return name ? name : "Controller";
}

/* ---- car profile ---------------------------------------------------------- */

static void save_car(void)
{
    nvs_handle_t h;
    if (nvs_open("vagscanner", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "car", (uint8_t)s_car);
        nvs_commit(h);
        nvs_close(h);
    }
}

static KwCar load_car(void)
{
    nvs_handle_t h;
    uint8_t v = KW_CAR_UNKNOWN;
    if (nvs_open("vagscanner", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "car", &v);
        nvs_close(h);
    }
    return v < KW_CAR_COUNT ? (KwCar)v : KW_CAR_UNKNOWN;
}

static void show_car(KwCar car, bool animate)
{
    const KwCarProfile *p = kw_car_profile(car);
    s_car = car;
    lv_image_set_src(s_hero, assets_hero(p->key));
    lv_image_set_src(s_detail_hero, assets_hero(p->key));
    lv_label_set_text(s_title, p4_text(p->name));
    if (car == KW_CAR_UNKNOWN && s_scanned && s_module_count)
        lv_label_set_text(s_subtitle, "Not one of yours  " LV_SYMBOL_BULLET "  tap the name to choose");
    else
        lv_label_set_text(s_subtitle, p4_text(p->detail));
    if (animate)
        lv_obj_fade_in(s_hero, 450, 0);
}

static void on_title(lv_event_t *e)
{
    (void)e;
    /* manual override: cycle through the profiles */
    s_car_manual = true;
    show_car((KwCar)((s_car + 1) % KW_CAR_COUNT), true);
    save_car();
}

/* ---- status pill ---------------------------------------------------------- */

static void update_pill(void)
{
    uint32_t c = C_MUTED;
    const char *t = "No cable";
    if (s_demo) {
        c = C_WARN;
        t = "Demo car";
    } else if (s_cable) {
        c = C_OK;
        t = "Cable ready";
    }
    lv_obj_set_style_bg_color(s_pill_dot, col(c), 0);
    lv_label_set_text(s_pill_label, t);
}

/* ---- module cards --------------------------------------------------------- */

static void set_card_status(module_t *m)
{
    if (!m->status)
        return;
    if (!m->supported) {
        lv_label_set_text(m->status, "KWP2000  " LV_SYMBOL_BULLET "  not supported");
        lv_obj_set_style_text_color(m->status, col(C_MUTED), 0);
    } else if (m->error) {
        lv_label_set_text(m->status, m->error == KW_ENOWAKE ? "No response - tap to retry" : "Read failed - tap to retry");
        lv_obj_set_style_text_color(m->status, col(C_WARN), 0);
    } else if (m->faults < 0) {
        lv_label_set_text(m->status, "Not checked");
        lv_obj_set_style_text_color(m->status, col(C_MUTED), 0);
    } else if (m->faults == 0) {
        lv_label_set_text(m->status, LV_SYMBOL_OK "  No faults");
        lv_obj_set_style_text_color(m->status, col(C_OK), 0);
    } else {
        lv_label_set_text_fmt(m->status, LV_SYMBOL_WARNING "  %d fault%s", m->faults, m->faults == 1 ? "" : "s");
        lv_obj_set_style_text_color(m->status, col(C_ERR), 0);
    }
}

static void open_module_index(int idx);
static void on_dash_open(lv_event_t *e);
static void on_health_open(lv_event_t *e);
static void on_wifi_open(lv_event_t *e);
static lv_obj_t *s_wifi_icon;
static void on_value_tile(lv_event_t *e);

static void on_card(lv_event_t *e)
{
    open_module_index((int)(intptr_t)lv_event_get_user_data(e));
}

static void add_card(int idx)
{
    module_t *m = &s_modules[idx];
    char buf[8];

    lv_obj_t *card = lv_button_create(s_grid);
    lv_obj_set_size(card, 360, 132);
    lv_obj_set_style_bg_color(card, col(C_SURFACE), 0);
    lv_obj_set_style_bg_color(card, col(C_SURFACE2), LV_STATE_PRESSED);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_add_event_cb(card, on_card, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    lv_obj_t *badge = surface(card, C_ACCENT, 16);
    lv_obj_set_size(badge, 60, 60);
    lv_obj_set_style_bg_opa(badge, LV_OPA_20, 0);
    snprintf(buf, sizeof(buf), "%02X", m->address);
    lv_obj_center(text(badge, &lv_font_montserrat_24, C_ACCENT, buf));

    lv_obj_t *name = text(card, &lv_font_montserrat_24, C_TEXT, module_name(m->address));
    lv_obj_set_pos(name, 76, 2);
    lv_obj_set_width(name, 248);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t *ident = text(card, &lv_font_montserrat_16, C_MUTED,
                          m->protocol == 0 ? "Tap to connect" : m->ident);
    m->ident_label = ident;
    lv_obj_set_pos(ident, 76, 36);
    lv_obj_set_size(ident, 248, 22);
    lv_label_set_long_mode(ident, LV_LABEL_LONG_DOT);

    m->status = text(card, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(m->status, LV_ALIGN_BOTTOM_LEFT, 76, 0);
    lv_obj_set_width(m->status, 248);
    lv_label_set_long_mode(m->status, LV_LABEL_LONG_DOT);
    set_card_status(m);

    lv_obj_fade_in(card, 300, 0);
}

static int module_index(uint8_t address)
{
    for (int i = 0; i < s_module_count; i++)
        if (s_modules[i].address == address)
            return i;
    return -1;
}

static int ensure_module(uint8_t address)
{
    int idx = module_index(address);
    if (idx >= 0 || s_module_count == MAX_MODULES)
        return idx;
    idx = s_module_count++;
    s_modules[idx] = (module_t){ .address = address, .supported = true, .faults = -1 };
    add_card(idx);
    return idx;
}

static int found_count(void)
{
    int count = 0;
    for (int i = 0; i < s_module_count; i++)
        count += s_modules[i].protocol != 0;
    return count;
}

static void clear_modules(void)
{
    s_current = -1;
    s_module_count = 0;
    lv_obj_clean(s_grid);
    lv_label_set_text(s_count_label, "");
    for (size_t i = 0; i < sizeof(LANDING_ADDRESSES); i++)
        ensure_module(LANDING_ADDRESSES[i]);
}

static void on_more_pick(lv_event_t *e)
{
    uint8_t address = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
    lv_msgbox_close_async(mbox);
    if (!s_busy)
        open_module_index(ensure_module(address));
}

static void on_more_modules(lv_event_t *e)
{
    (void)e;
    if (s_busy)
        return;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_size(mbox, 680, 880);
    lv_msgbox_add_title(mbox, "More modules");
    lv_msgbox_add_text(mbox, "Choose a controller to read its faults directly.");
    lv_obj_t *content = lv_msgbox_get_content(mbox);
    for (int address = 1; address <= 0x7F; address++) {
        const char *name = kw_module_name(address);
        bool landing = false;
        for (size_t i = 0; i < sizeof(LANDING_ADDRESSES); i++)
            landing |= LANDING_ADDRESSES[i] == address;
        if (!name || landing)
            continue;
        char label[64];
        snprintf(label, sizeof(label), "%02X  %s", address, name);
        lv_obj_t *b = button(content, label, C_SURFACE2, C_TEXT, on_more_pick, (void *)(intptr_t)address);
        lv_obj_set_size(b, LV_PCT(100), 64);
    }
    lv_msgbox_add_close_button(mbox);
}

/* ---- scanning ------------------------------------------------------------- */

static void start_scan(kl_request_t req)
{
    if (!kline_submit(&req))
        return;
    s_busy = s_scanning = true;
    s_scanned = false;
    memset(&s_votes, 0, sizeof(s_votes));
    s_car_manual = false;
    clear_modules();
    lv_label_set_text(s_scan_label, LV_SYMBOL_STOP "  Stop");
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);
    lv_obj_set_hidden(s_progress, false);
}

static void start_demo(FakeCar car)
{
    start_scan((kl_request_t){ .op = KL_DEMO, .demo_car = car });
}

static void on_scan(lv_event_t *e)
{
    (void)e;
    if (s_scanning) {
        kline_cancel();
        return;
    }
    if (s_busy)
        return;
    if (!s_cable) {
        lv_label_set_text(s_scan_status, "Plug the KKL cable into the P4, or tap Demo");
        return;
    }
    start_scan((kl_request_t){ .op = KL_SCAN });
}

static void on_scan_hold(lv_event_t *e)
{
    (void)e;
    static const FakeCar cars[] = { FAKE_CAR_EUROVAN, FAKE_CAR_HARLEQUIN, FAKE_CAR_GOLF4 };
    if (!s_busy)
        start_demo(cars[s_demo_next++ % 3]);
}

static void on_demo_pick(lv_event_t *e)
{
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
    FakeCar car = (FakeCar)(intptr_t)lv_event_get_user_data(e);
    lv_msgbox_close_async(mbox);
    if (!s_busy)
        start_demo(car);
}

static void on_demo(lv_event_t *e)
{
    (void)e;
    if (s_busy)
        return;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 640);
    lv_msgbox_add_title(mbox, "Demo car");
    lv_msgbox_add_text(mbox, "A simulated car with fault codes and live measuring blocks. "
                             "Nothing is sent to a real car.");
    static const struct { const char *label; FakeCar car; } choices[] = {
        { "EuroVan", FAKE_CAR_EUROVAN }, { "Harlequin", FAKE_CAR_HARLEQUIN }, { "Other VW", FAKE_CAR_GOLF4 },
    };
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); i++) {
        lv_obj_t *b = lv_msgbox_add_footer_button(mbox, choices[i].label);
        lv_obj_add_event_cb(b, on_demo_pick, LV_EVENT_CLICKED, (void *)(intptr_t)choices[i].car);
    }
    lv_msgbox_add_close_button(mbox);
}

/* ---- credits -------------------------------------------------------------- */

static void go_home(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(s_home, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
}

static void on_credits(lv_event_t *e)
{
    (void)e;
    lv_screen_load_anim(s_credits, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
}

static void build_credits(void)
{
    s_credits = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_credits, col(C_BG), 0);
    lv_obj_set_style_pad_all(s_credits, 32, 0);
    lv_obj_set_flex_flow(s_credits, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_credits, 20, 0);

    round_button(s_credits, LV_SYMBOL_LEFT, go_home);
    text(s_credits, &lv_font_montserrat_36, C_TEXT, "Photo credits");
    lv_obj_t *note = text(s_credits, &lv_font_montserrat_20, C_MUTED,
                          "Photos from Wikimedia Commons, used under their licences (cropped and resized).");
    lv_obj_set_width(note, LV_PCT(100));
    lv_obj_t *body = text(s_credits, &lv_font_montserrat_20, C_TEXT, assets_credits());
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_line_space(body, 10, 0);
    lv_obj_t *app = text(s_credits, &lv_font_montserrat_16, C_MUTED,
                         "Protocol after kw1281test by Greg Menounos (MIT). "
                         "Display and touch drivers after chipguyhere (MIT).");
    lv_obj_set_width(app, LV_PCT(100));
}

/* ---- home screen ---------------------------------------------------------- */

static void build_home(void)
{
    s_home = lv_screen_active();
    lv_obj_set_style_bg_color(s_home, col(C_BG), 0);
    lv_obj_set_scrollable(s_home, false);

    lv_obj_t *header = plain(s_home);
    lv_obj_set_size(header, SCREEN_W, 420);
    s_hero = lv_image_create(header);
    lv_obj_set_pos(s_hero, 0, -120);
    scrim(header, 220, 200);

    /* top bar */
    lv_obj_t *brand = text(s_home, &lv_font_montserrat_16, C_TEXT, "ESP32 VAG SCANNER");
    lv_obj_set_style_text_letter_space(brand, 3, 0);
    lv_obj_set_style_text_opa(brand, LV_OPA_80, 0);
    lv_obj_set_pos(brand, 32, 40);
    lv_obj_set_clickable(brand, true);
    lv_obj_set_ext_click_area(brand, 20);
    lv_obj_add_event_cb(brand, on_credits, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pill = surface(s_home, 0x000000, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(pill, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(pill, 18, 0);
    lv_obj_set_style_pad_ver(pill, 10, 0);
    lv_obj_set_flex_flow(pill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pill, 10, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -28, 28);
    s_wifi_icon = round_button(s_home, LV_SYMBOL_WIFI, on_wifi_open);
    lv_obj_set_size(s_wifi_icon, 60, 60);
    lv_obj_set_pos(s_wifi_icon, 496, 22);
    lv_obj_set_style_text_color(lv_obj_get_child(s_wifi_icon, 0), col(C_MUTED), 0);

    s_pill_dot = surface(pill, C_MUTED, LV_RADIUS_CIRCLE);
    lv_obj_set_size(s_pill_dot, 12, 12);
    s_pill_label = text(pill, &lv_font_montserrat_16, C_TEXT, "");

    /* car name over the bottom of the photo */
    s_title = text(s_home, &lv_font_montserrat_48, C_TEXT, "");
    lv_obj_set_pos(s_title, 32, 308);
    lv_obj_set_clickable(s_title, true);
    lv_obj_set_ext_click_area(s_title, 16);
    lv_obj_add_event_cb(s_title, on_title, LV_EVENT_CLICKED, NULL);
    s_subtitle = text(s_home, &lv_font_montserrat_20, C_MUTED, "");
    lv_obj_set_pos(s_subtitle, 34, 370);

    /* scan, and demo beside it */
    lv_obj_t *scan = button(s_home, NULL, C_ACCENT, C_TEXT, on_scan, NULL);
    lv_obj_add_event_cb(scan, on_scan_hold, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_set_size(scan, 344, 80);
    lv_obj_set_pos(scan, 32, 426);
    lv_obj_set_style_radius(scan, 24, 0);
    s_scan_label = text(scan, &lv_font_montserrat_28, C_TEXT, LV_SYMBOL_REFRESH "  Scan car");
    lv_obj_center(s_scan_label);

    lv_obj_t *dash = button(s_home, LV_SYMBOL_EYE_OPEN "  Dashboard", C_SURFACE, C_TEXT, on_dash_open, NULL);
    lv_obj_set_size(dash, 196, 80);
    lv_obj_set_pos(dash, 384, 426);
    lv_obj_set_style_radius(dash, 24, 0);

    lv_obj_t *demo = button(s_home, LV_SYMBOL_PLAY "  Demo", C_SURFACE, C_WARN, on_demo, NULL);
    lv_obj_set_size(demo, 172, 80);
    lv_obj_set_pos(demo, 596, 426);
    lv_obj_set_style_radius(demo, 24, 0);
    lv_obj_set_style_border_color(demo, col(C_WARN), 0);
    lv_obj_set_style_border_width(demo, 2, 0);

    s_progress = lv_bar_create(s_home);
    lv_obj_set_size(s_progress, 736, 6);
    lv_obj_set_pos(s_progress, 32, 520);
    lv_obj_set_style_bg_color(s_progress, col(C_SURFACE2), 0);
    lv_obj_set_style_bg_color(s_progress, col(C_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_range(s_progress, 0, 100);
    lv_obj_set_hidden(s_progress, true);

    s_scan_status = text(s_home, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(s_scan_status, 34, 540);

    /* controllers */
    lv_obj_t *heading = text(s_home, &lv_font_montserrat_16, C_MUTED, "OPEN A MODULE");
    lv_obj_set_style_text_letter_space(heading, 3, 0);
    lv_obj_set_pos(heading, 34, 640);
    s_count_label = text(s_home, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(s_count_label, LV_ALIGN_TOP_RIGHT, -34, 640);

    lv_obj_t *health = button(s_home, "EuroVan health checks", C_SURFACE, C_TEXT, on_health_open, NULL);
    lv_obj_set_size(health, 736, 64);
    lv_obj_set_pos(health, 32, 570);

    s_grid = plain(s_home);
    lv_obj_set_scrollable(s_grid, true);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_size(s_grid, 736, 496);
    lv_obj_set_pos(s_grid, 32, 674);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_grid, 16, 0);
    lv_obj_set_style_pad_column(s_grid, 16, 0);

    lv_obj_t *more = button(s_home, "More modules...", C_SURFACE, C_TEXT, on_more_modules, NULL);
    lv_obj_set_size(more, 736, 64);
    lv_obj_set_pos(more, 32, 1184);
    clear_modules();
}

/* ---- detail screen -------------------------------------------------------- */

static void show_tab(bool live)
{
    lv_obj_set_state(s_tab_faults, LV_STATE_CHECKED, !live);
    lv_obj_set_state(s_tab_live, LV_STATE_CHECKED, live);
    if (live) {
        lv_obj_set_hidden(s_panel_faults, true);
        lv_obj_set_hidden(s_panel_live, false);
    } else {
        lv_obj_set_hidden(s_panel_faults, false);
        lv_obj_set_hidden(s_panel_live, true);
    }
}

static void submit_current(kl_op_t op)
{
    if (s_busy || s_current < 0 ||
        (op != KL_FAULTS && (!s_modules[s_current].supported || s_modules[s_current].protocol == 0)))
        return;
    kl_request_t req = { .op = op, .address = s_modules[s_current].address, .group = s_group };
    if (kline_submit(&req))
        s_busy = true;
}

static void on_back(lv_event_t *e)
{
    if (s_live) {
        s_restart_live = false;
        kline_cancel();
    }
    s_group_pending = false;
    s_current = -1;
    go_home(e);
}

static void on_tab(lv_event_t *e)
{
    show_tab(lv_event_get_user_data(e) != NULL);
}

static void read_faults(void)
{
    if (s_busy)
        return;
    if (s_current < 0)
        return;
    module_t *m = &s_modules[s_current];
    m->faults = -1;
    m->error = 0;
    set_card_status(m);
    lv_obj_clean(s_fault_list);
    lv_label_set_text(s_fault_status, "Connecting and reading faults...");
    submit_current(KL_FAULTS);
}

static void on_read_faults(lv_event_t *e)
{
    (void)e;
    read_faults();
}

static void on_clear_confirmed(lv_event_t *e)
{
    lv_msgbox_close_async(lv_event_get_user_data(e));
    lv_label_set_text(s_fault_status, "Clearing...");
    submit_current(KL_CLEAR);
}

static void on_clear_cancel(lv_event_t *e)
{
    lv_msgbox_close_async(lv_event_get_user_data(e));
}

static void on_clear(lv_event_t *e)
{
    (void)e;
    if (s_busy || s_current < 0 || !s_modules[s_current].supported || s_modules[s_current].faults <= 0)
        return;
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 600);
    lv_msgbox_add_title(mbox, "Clear fault codes?");
    lv_msgbox_add_text(mbox, "Write them down first if you need them. Faults that are still present will come back.");
    lv_obj_t *yes = lv_msgbox_add_footer_button(mbox, "Clear");
    lv_obj_t *no = lv_msgbox_add_footer_button(mbox, "Cancel");
    lv_obj_set_style_bg_color(yes, col(C_ERR), 0);
    lv_obj_add_event_cb(yes, on_clear_confirmed, LV_EVENT_CLICKED, mbox);
    lv_obj_add_event_cb(no, on_clear_cancel, LV_EVENT_CLICKED, mbox);
}

static void set_group_label(void)
{
    lv_label_set_text_fmt(s_group_label, "Group %d", s_group);
}

static void read_group_once(void)
{
    lv_label_set_text_fmt(s_group_status, "Reading group %d...", s_group);
    submit_current(KL_GROUP);
}

static void step_group(int delta)
{
    if (s_current < 0 || !s_modules[s_current].supported || s_modules[s_current].protocol == 0)
        return;
    s_group += delta;
    if (s_group < 0)
        s_group = 0;
    if (s_group > 255)
        s_group = 255;
    set_group_label();

    if (s_live) {
        s_restart_live = true; /* the live loop stops, then restarts on s_group */
        kline_cancel();
    } else if (s_busy) {
        s_group_pending = true;
    } else {
        read_group_once();
    }
}

static void on_group_step(lv_event_t *e)
{
    step_group((int)(intptr_t)lv_event_get_user_data(e));
}

static void toggle_live(void)
{
    if (s_live) {
        s_restart_live = false;
        kline_cancel();
        return;
    }
    if (s_busy || s_current < 0 || !s_modules[s_current].supported || s_modules[s_current].protocol == 0)
        return;
    s_live = true;
    lv_label_set_text(s_live_label, LV_SYMBOL_STOP "  Stop");
    lv_label_set_text(s_group_status, "Connecting...");
    submit_current(KL_GROUP_LIVE);
}

static void on_live(lv_event_t *e)
{
    (void)e;
    toggle_live();
}

static lv_obj_t *tab_button(lv_obj_t *parent, const char *label, bool live)
{
    lv_obj_t *b = button(parent, NULL, C_SURFACE, C_MUTED, on_tab, live ? (void *)1 : NULL);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_height(b, 64);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, col(C_SURFACE2), LV_STATE_CHECKED);
    lv_obj_t *l = text(b, &lv_font_montserrat_20, C_MUTED, label);
    lv_obj_set_style_text_color(l, col(C_TEXT), LV_STATE_CHECKED);
    lv_obj_center(l);
    return b;
}

static void build_detail(void)
{
    s_detail = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_detail, col(C_BG), 0);
    lv_obj_set_scrollable(s_detail, false);

    /* photo header: the hero's car band, melting into the page */
    lv_obj_t *header = plain(s_detail);
    lv_obj_set_size(header, SCREEN_W, 380);
    s_detail_hero = lv_image_create(header);
    lv_obj_set_pos(s_detail_hero, 0, -40);
    scrim(header, 180, 200);
    lv_obj_t *back = round_button(s_detail, LV_SYMBOL_LEFT, on_back);
    lv_obj_set_pos(back, 24, 24);

    lv_obj_t *content = column(s_detail, 14);
    lv_obj_set_size(content, SCREEN_W - 64, SCREEN_H - 300 - 24);
    lv_obj_set_pos(content, 32, 300);

    s_detail_name = text(content, &lv_font_montserrat_48, C_TEXT, "");
    s_detail_sub = text(content, &lv_font_montserrat_20, C_MUTED, "");

    lv_obj_t *ident_card = surface(content, C_SURFACE, 18);
    lv_obj_set_width(ident_card, LV_PCT(100));
    lv_obj_set_style_pad_all(ident_card, 18, 0);
    s_detail_ident = text(ident_card, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_width(s_detail_ident, LV_PCT(100));
    lv_label_set_long_mode(s_detail_ident, LV_LABEL_LONG_WRAP);

    lv_obj_t *tabs = surface(content, C_SURFACE, 18);
    lv_obj_set_width(tabs, LV_PCT(100));
    lv_obj_set_style_pad_all(tabs, 6, 0);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 6, 0);
    s_tab_faults = tab_button(tabs, LV_SYMBOL_WARNING "  Fault codes", false);
    s_tab_live = tab_button(tabs, LV_SYMBOL_REFRESH "  Live data", true);

    /* faults */
    s_panel_faults = column(content, 14);
    lv_obj_set_flex_grow(s_panel_faults, 1);
    lv_obj_t *actions = row(s_panel_faults, 16);
    lv_obj_t *read = button(actions, LV_SYMBOL_DOWNLOAD "  Read faults", C_ACCENT, C_TEXT, on_read_faults, NULL);
    lv_obj_set_flex_grow(read, 1);
    lv_obj_set_height(read, 80);
    lv_obj_t *clear = button(actions, LV_SYMBOL_TRASH "  Clear", C_SURFACE, C_ERR, on_clear, NULL);
    lv_obj_set_flex_grow(clear, 1);
    lv_obj_set_height(clear, 80);
    lv_obj_set_style_border_color(clear, col(C_ERR), 0);
    lv_obj_set_style_border_width(clear, 2, 0);
    s_fault_status = text(s_panel_faults, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_width(s_fault_status, LV_PCT(100));
    lv_label_set_long_mode(s_fault_status, LV_LABEL_LONG_WRAP);
    s_fault_list = column(s_panel_faults, 12);
    lv_obj_set_flex_grow(s_fault_list, 1);
    lv_obj_set_scrollable(s_fault_list, true);
    lv_obj_set_scroll_dir(s_fault_list, LV_DIR_VER);

    /* live data */
    s_panel_live = column(content, 14);
    lv_obj_set_flex_grow(s_panel_live, 1);
    lv_obj_t *controls = row(s_panel_live, 12);
    lv_obj_t *minus = button(controls, LV_SYMBOL_MINUS, C_SURFACE, C_TEXT, on_group_step, (void *)(intptr_t)-1);
    lv_obj_set_size(minus, 80, 80);
    lv_obj_t *group_box = surface(controls, C_SURFACE, 20);
    lv_obj_set_height(group_box, 80);
    lv_obj_set_flex_grow(group_box, 1);
    s_group_label = text(group_box, &lv_font_montserrat_28, C_TEXT, "");
    lv_obj_center(s_group_label);
    set_group_label();
    lv_obj_t *plus = button(controls, LV_SYMBOL_PLUS, C_SURFACE, C_TEXT, on_group_step, (void *)(intptr_t)1);
    lv_obj_set_size(plus, 80, 80);
    lv_obj_t *live = button(controls, NULL, C_ACCENT, C_TEXT, on_live, NULL);
    lv_obj_set_size(live, 200, 80);
    s_live_label = text(live, &lv_font_montserrat_24, C_TEXT, LV_SYMBOL_PLAY "  Live");
    lv_obj_center(s_live_label);
    s_group_status = text(s_panel_live, &lv_font_montserrat_16, C_MUTED, "");

    lv_obj_t *values = plain(s_panel_live);
    lv_obj_set_width(values, LV_PCT(100));
    lv_obj_set_flex_flow(values, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(values, 16, 0);
    lv_obj_set_style_pad_column(values, 16, 0);
    for (int i = 0; i < MAX_VALUES; i++) {
        char idx[4];
        lv_obj_t *tile = surface(values, C_SURFACE, 20);
        lv_obj_set_size(tile, 360, 150);
        lv_obj_set_style_pad_all(tile, 20, 0);
        snprintf(idx, sizeof(idx), "%d", i + 1);
        s_value_names[i] = text(tile, &lv_font_montserrat_16, C_MUTED, idx);
        lv_obj_set_width(s_value_names[i], 320);
        lv_label_set_long_mode(s_value_names[i], LV_LABEL_LONG_DOT);
        s_value_labels[i] = text(tile, &lv_font_montserrat_36, C_TEXT, "");
        lv_obj_align(s_value_labels[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_width(s_value_labels[i], 320);
        lv_label_set_long_mode(s_value_labels[i], LV_LABEL_LONG_DOT);
        s_value_tiles[i] = tile;
        lv_obj_set_clickable(tile, true);
        lv_obj_add_event_cb(tile, on_value_tile, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_set_hidden(tile, true);
    }
    show_tab(false);
}

static void open_module_index(int idx)
{
    if (s_busy || idx < 0 || idx >= s_module_count)
        return;
    module_t *m = &s_modules[idx];
    if (s_live) {
        s_restart_live = false;
        kline_cancel();
    }
    s_current = idx;
    lv_label_set_text(s_detail_name, module_name(m->address));
    if (m->protocol)
        lv_label_set_text_fmt(s_detail_sub, "Address %02X  " LV_SYMBOL_BULLET "  KW%d", m->address, m->protocol);
    else
        lv_label_set_text_fmt(s_detail_sub, "Address %02X", m->address);
    lv_label_set_text(s_detail_ident, m->ident);
    s_group_pending = false;
    lv_label_set_text(s_fault_status, "Connecting...");
    lv_obj_clean(s_fault_list);
    lv_label_set_text(s_group_status, "Use - and + to pick a measuring group, then Live.");
    for (int i = 0; i < MAX_VALUES; i++)
        lv_obj_set_hidden(s_value_tiles[i], true);
    show_tab(false);
    lv_screen_load_anim(s_detail, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
    read_faults();
}

/* ---- dashboard -------------------------------------------------------------- */

/*
 * Four slots from kwcore/dashboard.c, each a live value with its trend. The
 * layout, units and fuel calibration factor live in NVS.
 */

#define DASH_POINTS 120

typedef struct {
    lv_obj_t *name, *source, *value, *unit, *footer, *chart, *top, *bottom, *reset, *freshness;
    lv_chart_series_t *series;
    float hist[DASH_POINTS];     /* metric; fuel slots hold l/100km */
    int hist_n;
    const char *metric_unit;     /* static string from kw_sensor_number */
    char text[32];               /* non-numeric fields */
    char label[32];
    char error[80];
    bool have;
    bool connecting;
    double updated_s;
} dash_tile_t;

static lv_obj_t *s_dash, *s_dash_status, *s_dash_run_label, *s_dash_units_label;
static dash_tile_t s_dt[KW_DASH_SLOTS];
static KwDashLayout s_layout;
static KwFuel s_fuel;
static double s_fuel_maf_s, s_fuel_speed_s;
static bool s_imperial = true;
static bool s_dash_running, s_dash_restart;
static int s_pick_slot;
static int s_pin_field;
static char s_value_label_text[MAX_VALUES][32];

/* LVGL's own printf has no %f: format numbers with libc, then set the text. */
static void label_printf(lv_obj_t *label, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(label, buf);
}

static void dash_save(void)
{
    nvs_handle_t h;
    if (nvs_open("vagscanner", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "dash", &s_layout, sizeof(s_layout));
        nvs_set_u8(h, "imperial", s_imperial);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void dash_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_layout);
    uint8_t imperial = 1;
    kw_dash_default(&s_layout);
    if (nvs_open("vagscanner", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "dash", &s_layout, &len) != ESP_OK || len != sizeof(s_layout))
            kw_dash_default(&s_layout);
        nvs_get_u8(h, "imperial", &imperial);
        nvs_close(h);
    }
    s_imperial = imperial;
    kw_fuel_init(&s_fuel, 1.0);
}

static const char *dash_fmt(double v, const char *unit)
{
    static char buf[24];
    const char *f = "%.1f";
    if (!strcmp(unit, "rpm") || !strcmp(unit, "km") || !strcmp(unit, "mi"))
        f = "%.0f";
    else if (!strcmp(unit, "V") || !strcmp(unit, "bar") || !strcmp(unit, "ms") || !strcmp(unit, "g/s"))
        f = "%.2f";
    snprintf(buf, sizeof(buf), f, v);
    return buf;
}

/* Coasting reads near-infinite: top out at 99.9 mpg (2.4 l/100km) like a trip computer. */
static double dash_economy(double l_per_100km)
{
    double l = l_per_100km < 235.214583 / 99.9 ? 235.214583 / 99.9 : l_per_100km;
    return s_imperial ? 235.214583 / l : l;
}

static double dash_shown(const KwDashSlot *slot, const dash_tile_t *t, double v, const char **unit)
{
    if (slot->kind == KW_SLOT_FUEL) {
        *unit = s_imperial ? "mpg" : "l/100km";
        return dash_economy(v);
    }
    return kw_unit_convert(v, t->metric_unit ? t->metric_unit : "", s_imperial, unit);
}

static const char *dash_note(const KwDashSlot *slot)
{
    for (int i = 0; kw_dash_preset(i); i++) {
        const KwDashPreset *p = kw_dash_preset(i);
        if (p->note && p->kind == slot->kind && p->address == slot->address && p->group == slot->group
            && p->field == slot->field)
            return p->note;
    }
    return NULL;
}

static void dash_redraw_chart(int i)
{
    dash_tile_t *t = &s_dt[i];
    const KwDashSlot *slot = &s_layout.slot[i];
    const char *unit = "";
    double lo = 0, hi = 0;
    for (int k = 0; k < t->hist_n; k++) {
        double v = dash_shown(slot, t, t->hist[k], &unit);
        if (k == 0 || v < lo)
            lo = v;
        if (k == 0 || v > hi)
            hi = v;
    }
    /* widen the range so sensor noise doesn't fill the chart */
    double minimum = !strcmp(unit, "V") ? 2 : (!strcmp(unit, "°C") || !strcmp(unit, "°F")) ? 20
                   : (!strcmp(unit, "mpg") || !strcmp(unit, "l/100km")) ? 10 : !strcmp(unit, "rpm") ? 500
                   : (fabs(hi) * 0.1 > 1 ? fabs(hi) * 0.1 : 1);
    double span = (hi - lo > minimum ? hi - lo : minimum) * 1.1, mid = (lo + hi) / 2;
    lo = mid - span / 2;
    hi = mid + span / 2;
    lv_chart_set_axis_range(t->chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(lo * 10), (int32_t)(hi * 10));
    lv_chart_set_all_values(t->chart, t->series, LV_CHART_POINT_NONE);
    int first = DASH_POINTS - t->hist_n;
    for (int k = 0; k < t->hist_n; k++)
        lv_chart_set_series_value_by_id(t->chart, t->series, first + k,
                                        (int32_t)(dash_shown(slot, t, t->hist[k], &unit) * 10));
    lv_chart_refresh(t->chart);
    if (t->hist_n) {
        lv_label_set_text(t->top, dash_fmt(hi, unit));
        lv_label_set_text(t->bottom, dash_fmt(lo, unit));
    } else {
        lv_label_set_text(t->top, "");
        lv_label_set_text(t->bottom, "");
    }
}

static void dash_refresh_age(int i)
{
    dash_tile_t *t = &s_dt[i];
    if (s_layout.slot[i].kind == KW_SLOT_EMPTY) {
        lv_label_set_text(t->freshness, "");
        return;
    }
    const char *state = t->connecting ? (t->error[0] ? "Retrying" : "Connecting") : "Updated";
    if (t->updated_s > 0) {
        unsigned long age = (unsigned long)((esp_timer_get_time() / 1e6) - t->updated_s);
        if (t->connecting)
            label_printf(t->freshness, "%s; data %lus old", state, age);
        else
            label_printf(t->freshness, "Updated %lus ago", age);
    } else {
        lv_label_set_text(t->freshness, t->connecting ? state : "Waiting for data");
    }
}

static void dash_age_timer(lv_timer_t *timer)
{
    (void)timer;
    if (lv_screen_active() == s_dash)
        for (int i = 0; i < KW_DASH_SLOTS; i++) dash_refresh_age(i);
}

static void dash_refresh_tile(int i)
{
    dash_tile_t *t = &s_dt[i];
    const KwDashSlot *slot = &s_layout.slot[i];
    const char *unit = "";

    lv_obj_set_hidden(t->reset, slot->kind != KW_SLOT_FUEL);
    switch (slot->kind) {
    case KW_SLOT_EMPTY:
        lv_label_set_text(t->name, "Empty slot");
        lv_label_set_text(t->source, "Tap " LV_SYMBOL_SETTINGS " to choose a value");
        lv_label_set_text(t->value, "");
        lv_label_set_text(t->unit, "");
        lv_label_set_text(t->footer, "");
        break;
    case KW_SLOT_SENSOR:
        lv_label_set_text(t->name, slot->name);
        if (t->label[0] && strcmp(t->label, slot->name))
            label_printf(t->source, "%02X  " LV_SYMBOL_BULLET "  Group %d  " LV_SYMBOL_BULLET "  Field %d  "
                                  LV_SYMBOL_BULLET "  %s", slot->address, slot->group, slot->field, t->label);
        else
            label_printf(t->source, "%02X  " LV_SYMBOL_BULLET "  Group %d  " LV_SYMBOL_BULLET "  Field %d",
                                  slot->address, slot->group, slot->field);
        if (t->have && t->hist_n) {
            double v = dash_shown(slot, t, t->hist[t->hist_n - 1], &unit);
            lv_label_set_text(t->value, dash_fmt(v, unit));
            lv_label_set_text(t->unit, unit);
        } else {
            lv_label_set_text(t->value, t->text[0] ? t->text : "--");
            lv_label_set_text(t->unit, "");
        }
        if (t->error[0]) {
            lv_obj_set_style_text_color(t->footer, col(C_WARN), 0);
            label_printf(t->footer, LV_SYMBOL_WARNING "  %s", t->error);
        } else {
            lv_obj_set_style_text_color(t->footer, col(C_MUTED), 0);
            const char *note = dash_note(slot);
            lv_label_set_text(t->footer, note ? note : "");
        }
        break;
    case KW_SLOT_FUEL: {
        lv_label_set_text(t->name, slot->name);
        label_printf(t->source, "%02X  " LV_SYMBOL_BULLET "  Groups %d + %d  " LV_SYMBOL_BULLET
                              "  estimated from air mass and speed", KW_FUEL_ADDRESS, KW_FUEL_MAF_GROUP,
                              KW_FUEL_SPEED_GROUP);
        double instant = kw_fuel_instant(&s_fuel);
        if (instant > 0) {
            label_printf(t->value, "%.1f", dash_economy(instant));
            lv_label_set_text(t->unit, s_imperial ? "mpg" : "l/100km");
        } else if (s_fuel.have_maf && s_fuel.have_speed) {
            /* standing still: no distance, so 0 mpg (l/100km has no finite value) */
            lv_label_set_text(t->value, s_imperial ? "0.0" : "--");
            lv_label_set_text(t->unit, s_imperial ? "mpg  stopped" : "l/100km  stopped");
        } else {
            lv_label_set_text(t->value, "--");
            lv_label_set_text(t->unit, s_imperial ? "mpg" : "l/100km");
        }
        if (t->error[0]) {
            lv_obj_set_style_text_color(t->footer, col(C_WARN), 0);
            label_printf(t->footer, LV_SYMBOL_WARNING "  %s", t->error);
        } else {
            const char *du, *vu;
            double dist = kw_unit_convert(s_fuel.trip_km, "km", s_imperial, &du);
            double used = kw_unit_convert(s_fuel.trip_l, "l", s_imperial, &vu);
            double avg = kw_fuel_average(&s_fuel);
            char avg_text[24] = "--";
            if (avg > 0)
                snprintf(avg_text, sizeof(avg_text), "%.1f %s", dash_economy(avg), s_imperial ? "mpg" : "l/100km");
            lv_obj_set_style_text_color(t->footer, col(C_MUTED), 0);
            label_printf(t->footer, "Trip %s  " LV_SYMBOL_BULLET "  %.1f %s  " LV_SYMBOL_BULLET "  %.2f %s",
                                  avg_text, dist, du, used, vu);
        }
        break;
    }
    }
    dash_redraw_chart(i);
    dash_refresh_age(i);
}

static void dash_push(dash_tile_t *t, double v)
{
    if (t->hist_n == DASH_POINTS) {
        memmove(t->hist, t->hist + 1, sizeof(t->hist[0]) * (DASH_POINTS - 1));
        t->hist_n--;
    }
    t->hist[t->hist_n++] = (float)v;
}

static void dash_describe(char *out, size_t len, int err, uint8_t address, int group)
{
    if (err == KW_ENOWAKE)
        snprintf(out, len, "No response from %02X. Retrying.", address);
    else if (err == KW_ENAK)
        snprintf(out, len, "%02X has no group %d. Pick another in Live data.", address, group);
    else if (err == KW_EKWP2000)
        snprintf(out, len, "%02X uses KWP2000, which isn't supported.", address);
    else
        snprintf(out, len, "%s", kw_strerror(err));
}

static void dash_apply(const kl_event_t *ev)
{
    if (ev->address == 0) { /* no cable */
        lv_label_set_text(s_dash_status, "No cable. Plug in the KKL cable, or start a demo car first.");
        return;
    }
    if (ev->dash_state == KW_DASH_CONNECTING) {
        label_printf(s_dash_status, "Connecting to %02X...", ev->address);
        for (int i = 0; i < KW_DASH_SLOTS; i++) {
            const KwDashSlot *slot = &s_layout.slot[i];
            if ((slot->kind == KW_SLOT_SENSOR && slot->address == ev->address)
                || (slot->kind == KW_SLOT_FUEL && ev->address == KW_FUEL_ADDRESS)) {
                s_dt[i].connecting = true;
                dash_refresh_age(i);
            }
        }
        return;
    }
    if (ev->ok)
        lv_label_set_text(s_dash_status, s_demo ? "Live  " LV_SYMBOL_BULLET "  demo car" : "Live");
    else
        label_printf(s_dash_status, "Read failed on %02X; retrying", ev->address);
    double t_s = esp_timer_get_time() / 1e6;
    if (ev->ok && ev->address == KW_FUEL_ADDRESS) {
        double v;
        if (ev->group == KW_FUEL_MAF_GROUP && ev->count >= KW_FUEL_MAF_FIELD
            && kw_sensor_number(&ev->sensors[KW_FUEL_MAF_FIELD - 1], &v, NULL)) {
            kw_fuel_maf(&s_fuel, v, t_s);
            s_fuel_maf_s = t_s;
        }
        if (ev->group == KW_FUEL_SPEED_GROUP && ev->count >= KW_FUEL_SPEED_FIELD
            && kw_sensor_number(&ev->sensors[KW_FUEL_SPEED_FIELD - 1], &v, NULL)) {
            kw_fuel_speed(&s_fuel, v, t_s);
            s_fuel_speed_s = t_s;
        }
    }

    for (int i = 0; i < KW_DASH_SLOTS; i++) {
        const KwDashSlot *slot = &s_layout.slot[i];
        dash_tile_t *t = &s_dt[i];
        if (slot->kind == KW_SLOT_SENSOR) {
            if (slot->address != ev->address || slot->group != ev->group)
                continue;
            t->connecting = false;
            if (!ev->ok) {
                dash_describe(t->error, sizeof(t->error), ev->err, ev->address, ev->group);
            } else if (slot->field < 1 || slot->field > ev->count) {
                snprintf(t->error, sizeof(t->error), "Group %d has only %d fields.", slot->group, ev->count);
            } else {
                const SensorValue *sv = &ev->sensors[slot->field - 1];
                double v;
                t->error[0] = '\0';
                t->updated_s = t_s;
                snprintf(t->label, sizeof(t->label), "%s", kw_label(ev->ident, slot->group, slot->field, sv->sensor_id));
                if (kw_sensor_number(sv, &v, &t->metric_unit)) {
                    t->have = true;
                    t->text[0] = '\0';
                    dash_push(t, v);
                } else {
                    t->have = false;
                    kw_format_sensor(sv, t->text, sizeof(t->text));
                }
            }
        } else if (slot->kind == KW_SLOT_FUEL) {
            if (ev->address != KW_FUEL_ADDRESS
                || (ev->group != KW_FUEL_MAF_GROUP && ev->group != KW_FUEL_SPEED_GROUP))
                continue;
            t->connecting = false;
            if (!ev->ok) {
                dash_describe(t->error, sizeof(t->error), ev->err, ev->address, ev->group);
            } else {
                t->error[0] = '\0';
                if (s_fuel_maf_s > 0 && s_fuel_speed_s > 0)
                    t->updated_s = s_fuel_maf_s < s_fuel_speed_s ? s_fuel_maf_s : s_fuel_speed_s;
                double instant = kw_fuel_instant(&s_fuel);
                if (instant > 0 && ev->group == KW_FUEL_SPEED_GROUP)
                    dash_push(t, instant);
            }
        } else {
            continue;
        }
        dash_refresh_tile(i);
    }
}

static void dash_set_running(bool running)
{
    s_dash_running = running;
    if (!running)
        for (int i = 0; i < KW_DASH_SLOTS; i++) {
            s_dt[i].connecting = false;
            dash_refresh_age(i);
        }
    lv_label_set_text(s_dash_run_label, running ? LV_SYMBOL_STOP "  Stop" : LV_SYMBOL_PLAY "  Start");
}

static void dash_start(void)
{
    if (s_busy || s_dash_running)
        return;
    kl_request_t req = { .op = KL_DASH };
    req.plan_count = kw_dash_plan(&s_layout, req.plan, KW_DASH_PLAN_MAX);
    if (req.plan_count == 0) {
        lv_label_set_text(s_dash_status, "Choose something to show first.");
        return;
    }
    if (!s_cable && !s_demo) {
        lv_label_set_text(s_dash_status, "No cable. Plug in the KKL cable, or start a demo car first.");
        return;
    }
    if (!kline_submit(&req))
        return;
    s_busy = true;
    dash_set_running(true);
    for (int i = 0; i < KW_DASH_SLOTS; i++) {
        s_dt[i].connecting = s_layout.slot[i].kind != KW_SLOT_EMPTY;
        dash_refresh_age(i);
    }
    lv_label_set_text(s_dash_status, "Connecting...");
}

static void on_dash_run(lv_event_t *e)
{
    (void)e;
    if (s_dash_running) {
        s_dash_restart = false;
        kline_cancel();
    } else {
        dash_start();
    }
}

static void on_dash_units(lv_event_t *e)
{
    (void)e;
    s_imperial = !s_imperial;
    lv_label_set_text(s_dash_units_label, s_imperial ? "US" : "Metric");
    dash_save();
    for (int i = 0; i < KW_DASH_SLOTS; i++)
        dash_refresh_tile(i);
}

static void on_dash_back(lv_event_t *e)
{
    if (s_dash_running) {
        s_dash_restart = false;
        kline_cancel();
    }
    go_home(e);
}

static void on_dash_open(lv_event_t *e)
{
    (void)e;
    if (s_busy && !s_dash_running)
        return;
    for (int i = 0; i < KW_DASH_SLOTS; i++)
        dash_refresh_tile(i);
    lv_screen_load_anim(s_dash, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
}

/* Puts `slot` in place `i`; a running dashboard restarts on the new plan. */
static void dash_set_slot(int i, const KwDashSlot *slot)
{
    s_layout.slot[i] = *slot;
    memset(&s_dt[i].hist, 0, sizeof(s_dt[i].hist));
    s_dt[i].hist_n = 0;
    s_dt[i].have = false;
    s_dt[i].updated_s = 0;
    s_dt[i].connecting = false;
    s_dt[i].text[0] = s_dt[i].label[0] = s_dt[i].error[0] = '\0';
    dash_save();
    dash_refresh_tile(i);
    if (s_dash_running) {
        s_dash_restart = true;
        kline_cancel();
    }
}

static void on_preset_pick(lv_event_t *e)
{
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
    int p = (int)(intptr_t)lv_event_get_user_data(e);
    KwDashSlot slot = { 0 };
    if (p >= 0)
        kw_dash_slot_from_preset(&slot, kw_dash_preset(p));
    lv_msgbox_close_async(mbox);
    dash_set_slot(s_pick_slot, &slot);
}

static void on_slot_settings(lv_event_t *e)
{
    s_pick_slot = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 640);
    lv_msgbox_add_title(mbox, "Show in this slot");
    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(content, 8, 0);
    text(content, &lv_font_montserrat_16, C_MUTED,
         "EuroVan presets (VW standard groups). For any other value, open a "
         "controller's Live data and tap the value.");
    for (int i = 0; kw_dash_preset(i); i++) {
        lv_obj_t *b = button(content, kw_dash_preset(i)->name, C_SURFACE2, C_TEXT, NULL, NULL);
        lv_obj_set_size(b, LV_PCT(100), 60);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_add_event_cb(b, on_preset_pick, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_t *clear = button(content, "Clear slot", C_SURFACE2, C_ERR, NULL, NULL);
    lv_obj_set_size(clear, LV_PCT(100), 60);
    lv_obj_set_style_radius(clear, 14, 0);
    lv_obj_add_event_cb(clear, on_preset_pick, LV_EVENT_CLICKED, (void *)(intptr_t)-1);
    lv_msgbox_add_close_button(mbox);
}

static void on_reset_trip(lv_event_t *e)
{
    (void)e;
    kw_fuel_reset_trip(&s_fuel);
    for (int i = 0; i < KW_DASH_SLOTS; i++)
        if (s_layout.slot[i].kind == KW_SLOT_FUEL)
            dash_refresh_tile(i);
}

/* Live data: tap a value to put it on the dashboard. */
static void on_pin_slot(lv_event_t *e)
{
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_msgbox_close_async(mbox);
    if (s_current < 0)
        return;
    KwDashSlot slot = { .kind = KW_SLOT_SENSOR, .address = s_modules[s_current].address,
                        .group = (uint8_t)s_group, .field = (uint8_t)(s_pin_field + 1) };
    snprintf(slot.name, sizeof(slot.name), "%s", s_value_label_text[s_pin_field]);
    dash_set_slot(i, &slot);
    label_printf(s_group_status, "Field %d is on dashboard slot %d", s_pin_field + 1, i + 1);
}

static void on_value_tile(lv_event_t *e)
{
    s_pin_field = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 640);
    lv_msgbox_add_title(mbox, "Show on dashboard");
    lv_msgbox_add_text(mbox, s_value_label_text[s_pin_field]);
    for (int i = 0; i < KW_DASH_SLOTS; i++) {
        char label[12];
        snprintf(label, sizeof(label), "Slot %d", i + 1);
        lv_obj_t *b = lv_msgbox_add_footer_button(mbox, label);
        lv_obj_add_event_cb(b, on_pin_slot, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_msgbox_add_close_button(mbox);
}

static void build_dash(void)
{
    s_dash = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_dash, col(C_BG), 0);
    lv_obj_set_scrollable(s_dash, false);

    lv_obj_t *back = round_button(s_dash, LV_SYMBOL_LEFT, on_dash_back);
    lv_obj_set_style_bg_color(back, col(C_SURFACE), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_pos(back, 24, 24);
    lv_obj_t *title = text(s_dash, &lv_font_montserrat_36, C_TEXT, "Dashboard");
    lv_obj_set_pos(title, 108, 32);

    lv_obj_t *units = button(s_dash, NULL, C_SURFACE, C_TEXT, on_dash_units, NULL);
    lv_obj_set_size(units, 130, 64);
    lv_obj_set_pos(units, 438, 24);
    s_dash_units_label = text(units, &lv_font_montserrat_20, C_TEXT, s_imperial ? "US" : "Metric");
    lv_obj_center(s_dash_units_label);
    lv_obj_t *run = button(s_dash, NULL, C_ACCENT, C_TEXT, on_dash_run, NULL);
    lv_obj_set_size(run, 184, 64);
    lv_obj_set_pos(run, 584, 24);
    s_dash_run_label = text(run, &lv_font_montserrat_24, C_TEXT, "");
    lv_obj_center(s_dash_run_label);

    s_dash_status = text(s_dash, &lv_font_montserrat_16, C_MUTED, "Choose a value for each slot, then Start.");
    lv_obj_set_pos(s_dash_status, 34, 104);

    for (int i = 0; i < KW_DASH_SLOTS; i++) {
        dash_tile_t *t = &s_dt[i];
        lv_obj_t *tile = surface(s_dash, C_SURFACE, 20);
        lv_obj_set_size(tile, 736, 266);
        lv_obj_set_pos(tile, 32, 136 + i * (266 + 14));
        lv_obj_set_style_pad_all(tile, 18, 0);

        t->name = text(tile, &lv_font_montserrat_24, C_TEXT, "");
        t->source = text(tile, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_pos(t->source, 0, 32);
        lv_obj_set_width(t->source, 620);
        lv_label_set_long_mode(t->source, LV_LABEL_LONG_DOT);
        lv_obj_t *cfg = button(tile, LV_SYMBOL_SETTINGS, C_SURFACE2, C_MUTED, on_slot_settings, (void *)(intptr_t)i);
        lv_obj_set_size(cfg, 64, 56);
        lv_obj_set_style_radius(cfg, 14, 0);
        lv_obj_align(cfg, LV_ALIGN_TOP_RIGHT, 0, -4);

        /* value on the left, trend on the right */
        t->value = text(tile, &lv_font_montserrat_48, C_TEXT, "");
        lv_obj_set_pos(t->value, 0, 72);
        t->unit = text(tile, &lv_font_montserrat_20, C_MUTED, "");
        lv_obj_set_pos(t->unit, 2, 132);
        t->freshness = text(tile, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_pos(t->freshness, 0, 168);
        lv_obj_set_width(t->freshness, 220);
        lv_label_set_long_mode(t->freshness, LV_LABEL_LONG_DOT);

        t->chart = lv_chart_create(tile);
        lv_obj_set_size(t->chart, 400, 128);
        lv_obj_set_pos(t->chart, 230, 66);
        lv_chart_set_type(t->chart, LV_CHART_TYPE_LINE);
        lv_chart_set_point_count(t->chart, DASH_POINTS);
        lv_chart_set_div_line_count(t->chart, 3, 0);
        lv_obj_set_style_bg_opa(t->chart, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(t->chart, 0, 0);
        lv_obj_set_style_pad_all(t->chart, 0, 0);
        lv_obj_set_style_line_color(t->chart, col(C_MUTED), LV_PART_MAIN);
        lv_obj_set_style_line_opa(t->chart, LV_OPA_20, LV_PART_MAIN);
        lv_obj_set_style_line_width(t->chart, 2, LV_PART_ITEMS);
        lv_obj_set_style_width(t->chart, 0, LV_PART_INDICATOR);
        lv_obj_set_style_height(t->chart, 0, LV_PART_INDICATOR);
        t->series = lv_chart_add_series(t->chart, col(C_ACCENT), LV_CHART_AXIS_PRIMARY_Y);
        t->top = text(tile, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_pos(t->top, 642, 62);
        t->bottom = text(tile, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_pos(t->bottom, 642, 178);

        t->footer = text(tile, &lv_font_montserrat_16, C_MUTED, "");
        lv_obj_set_width(t->footer, 520);
        lv_label_set_long_mode(t->footer, LV_LABEL_LONG_DOT);
        lv_obj_align(t->footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        t->reset = button(tile, "Reset trip", C_SURFACE2, C_TEXT, on_reset_trip, NULL);
        lv_obj_set_size(t->reset, 170, 44);
        lv_obj_set_style_radius(t->reset, 12, 0);
        lv_obj_align(t->reset, LV_ALIGN_BOTTOM_RIGHT, 0, 8);
    }
    dash_set_running(false);
    lv_timer_create(dash_age_timer, 1000, NULL);
}

/* ---- EuroVan health checks ------------------------------------------------ */

static lv_obj_t *s_health, *s_health_picker, *s_health_run_label, *s_health_status;
static lv_obj_t *s_health_intro, *s_health_prep, *s_health_result_label, *s_health_values;
static lv_obj_t *s_health_limits, *s_health_confirm[4], *s_health_saved;
static lv_obj_t *s_health_save, *s_health_drip, *s_health_dry;
static KwHealthState s_health_state;
static KwHealthFrame s_health_frame;
static KwHealthResult s_health_result;
static int s_health_check;
static unsigned s_health_confirmations;
static bool s_health_running, s_health_have, s_health_demo;
static double s_health_received;

static void health_evaluate(void)
{
    if (!s_health_have) return;
    double now = s_health_frame.finished_s + esp_timer_get_time() / 1e6 - s_health_received;
    kw_health_evaluate(&s_health_state, &s_health_frame, s_health_confirmations, now, &s_health_result);
    lv_label_set_text(s_health_result_label, s_health_result.message);
    lv_obj_set_style_text_color(s_health_result_label, col(s_health_result.status == KW_HEALTH_ATTENTION ? C_ERR
        : s_health_result.status == KW_HEALTH_READY || s_health_result.status == KW_HEALTH_WITHIN_LIMITS ? C_OK : C_WARN), 0);
    label_printf(s_health_status, "%s%s%s | observed %.0f s", s_health_demo ? "SIMULATION | " : "",
                 s_health_running ? "" : "Last sample | ",
                 kw_health_status_name(s_health_result.status), s_health_result.observed_s);
    lv_label_set_text(s_health_values, p4_text(s_health_result.readings));
    bool ready = s_health_running && s_health_result.status == KW_HEALTH_READY;
    lv_obj_set_state(s_health_drip, LV_STATE_DISABLED, !ready);
    lv_obj_set_state(s_health_dry, LV_STATE_DISABLED, !ready);
    lv_obj_set_state(s_health_save, LV_STATE_DISABLED, false);
}

static void health_timer(lv_timer_t *timer)
{
    (void)timer;
    if (s_health_running) health_evaluate();
}

static void health_load_saved(void)
{
    char key[16], saved[2048];
    snprintf(key, sizeof(key), "health%d", s_health_check);
    nvs_handle_t h;
    size_t len = sizeof(saved);
    if (nvs_open("vagscanner", NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_str(h, key, saved, &len);
        nvs_close(h);
        if (err == ESP_OK) { lv_label_set_text(s_health_saved, saved); return; }
    }
    lv_label_set_text(s_health_saved, "No saved snapshot for this check on this device.");
}

static void health_reset(void)
{
    const KwHealthProcedure *p = kw_health_procedure(s_health_check);
    kw_health_init(&s_health_state, s_health_check);
    s_health_confirmations = 0;
    s_health_have = false;
    memset(&s_health_frame, 0, sizeof(s_health_frame));
    memset(&s_health_result, 0, sizeof(s_health_result));
    s_health_result.status = KW_HEALTH_INCONCLUSIVE;
    lv_label_set_text(s_health_intro, p->description);
    lv_label_set_text(s_health_prep, p->preparation);
    lv_label_set_text(s_health_limits, p->limitations);
    lv_label_set_text(s_health_result_label, "Start to verify the controller, then confirm the prerequisites.");
    lv_label_set_text(s_health_values, "");
    lv_label_set_text(s_health_status, "EuroVan only | read-only procedures");
    for (int i = 0; i < 4; i++) {
        lv_obj_set_hidden(s_health_confirm[i], !p->confirmation[i]);
        lv_obj_set_state(s_health_confirm[i], LV_STATE_CHECKED, false);
        lv_obj_set_state(s_health_confirm[i], LV_STATE_DISABLED, true);
        if (p->confirmation[i]) lv_checkbox_set_text(s_health_confirm[i], p->confirmation[i]);
    }
    lv_obj_set_state(s_health_save, LV_STATE_DISABLED, true);
    lv_obj_set_state(s_health_drip, LV_STATE_DISABLED, true);
    lv_obj_set_state(s_health_dry, LV_STATE_DISABLED, true);
    health_load_saved();
}

static void on_health_pick(lv_event_t *e)
{
    (void)e;
    if (s_health_running) return;
    s_health_check = lv_dropdown_get_selected(s_health_picker);
    health_reset();
}

static void on_health_confirm(lv_event_t *e)
{
    if (!s_health_running) return;
    unsigned bit = 1u << (unsigned)(intptr_t)lv_event_get_user_data(e);
    if (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)) s_health_confirmations |= bit;
    else s_health_confirmations &= ~bit;
    health_evaluate();
}

static void on_health_run(lv_event_t *e)
{
    (void)e;
    if (s_health_running) { kline_cancel(); return; }
    if (s_busy) return;
    if (!s_cable && !s_demo) {
        lv_label_set_text(s_health_status, "Connect a KKL cable or select the EuroVan demo first."); return;
    }
    health_reset();
    s_health_demo = s_demo;
    kl_request_t req = { .op = KL_HEALTH, .health_check = s_health_check };
    if (!kline_submit(&req)) {
        lv_label_set_text(s_health_status, "Cable task is busy. Try again."); return;
    }
    s_busy = s_health_running = true;
    lv_obj_set_state(s_health_picker, LV_STATE_DISABLED, true);
    for (int i = 0; i < 4; i++) lv_obj_set_state(s_health_confirm[i], LV_STATE_DISABLED, false);
    lv_label_set_text(s_health_run_label, "Stop");
    lv_label_set_text(s_health_status, "Connecting and verifying controller identity...");
}

static void on_health_back(lv_event_t *e)
{
    if (s_health_running) kline_cancel();
    go_home(e);
}

static void on_health_open(lv_event_t *e)
{
    (void)e;
    if (s_busy && !s_health_running) return;
    lv_screen_load_anim(s_health, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
}

static void on_health_source(lv_event_t *e)
{
    (void)e;
    const KwHealthProcedure *p = kw_health_procedure(s_health_check);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 720);
    lv_msgbox_add_title(mbox, "Factory procedure source");
    lv_msgbox_add_text(mbox, p->source_title);
    lv_obj_t *qr = lv_qrcode_create(lv_msgbox_get_content(mbox));
    lv_qrcode_set_size(qr, 400);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, p->source_url, strlen(p->source_url));
    lv_msgbox_add_close_button(mbox);
}

static void on_health_save(lv_event_t *e)
{
    if (!s_health_have) return;
    health_evaluate();
    int observation = (int)(intptr_t)lv_event_get_user_data(e);
    if (observation && (!s_health_running || s_health_result.status != KW_HEALTH_READY)) return;
    const KwHealthProcedure *p = kw_health_procedure(s_health_check);
    char saved[2048], key[16];
    int n = snprintf(saved, sizeof(saved), "%s%s\n%s\n%s\n%s\n%s\n%s\nConfirmed by user:\n",
        s_health_demo ? "SIMULATION | " : "", p->title, s_health_frame.ident,
        kw_health_status_name(s_health_result.status), s_health_result.message, s_health_result.readings,
        observation == 1 ? "User observed continued dripping after initial overflow-tube fluid."
        : observation == 2 ? "User observed no further flow after initial overflow-tube fluid; topping up indicated."
                           : "Snapshot only; no physical fluid-level observation recorded.");
    for (int i = 0; i < 4 && n >= 0 && n < (int)sizeof(saved); i++)
        if (p->confirmation[i] && (s_health_confirmations & (1u << i)))
            n += snprintf(saved + n, sizeof(saved) - n, "- %s\n", p->confirmation[i]);
    snprintf(key, sizeof(key), "health%d", s_health_check);
    nvs_handle_t h;
    esp_err_t err = nvs_open("vagscanner", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, key, saved);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err == ESP_OK) health_load_saved();
    else lv_label_set_text(s_health_saved, "Snapshot could not be saved. Try again.");
}

static lv_obj_t *health_paragraph(lv_obj_t *parent, const char *str)
{
    lv_obj_t *l = text(parent, &lv_font_montserrat_20, C_TEXT, str);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void build_health(void)
{
    s_health = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_health, col(C_BG), 0);
    lv_obj_set_scrollable(s_health, false);
    round_button(s_health, LV_SYMBOL_LEFT, on_health_back);
    lv_obj_t *title = text(s_health, &lv_font_montserrat_28, C_TEXT, "EuroVan health checks");
    lv_obj_set_pos(title, 88, 30);
    lv_obj_t *run = button(s_health, NULL, C_ACCENT, C_TEXT, on_health_run, NULL);
    lv_obj_set_size(run, 130, 64); lv_obj_set_pos(run, 638, 20);
    s_health_run_label = text(run, &lv_font_montserrat_24, C_TEXT, "Start");
    lv_obj_center(s_health_run_label);

    lv_obj_t *body = column(s_health, 20);
    lv_obj_set_size(body, 736, 1130); lv_obj_set_pos(body, 32, 110);
    lv_obj_set_scrollable(body, true); lv_obj_set_scroll_dir(body, LV_DIR_VER);
    s_health_picker = lv_dropdown_create(body);
    lv_obj_set_width(s_health_picker, LV_PCT(100));
    lv_dropdown_set_options(s_health_picker, "ATF level assistant\nCamshaft timing / chain assessment\nMisfire activity\nECU supply voltage");
    lv_obj_add_event_cb(s_health_picker, on_health_pick, LV_EVENT_VALUE_CHANGED, NULL);
    s_health_intro = health_paragraph(body, "");
    s_health_prep = health_paragraph(body, "");
    s_health_status = health_paragraph(body, "");
    s_health_result_label = health_paragraph(body, "");
    s_health_values = health_paragraph(body, "");
    for (int i = 0; i < 4; i++) {
        s_health_confirm[i] = lv_checkbox_create(body);
        lv_obj_set_width(s_health_confirm[i], LV_PCT(100));
        lv_obj_set_style_pad_ver(s_health_confirm[i], 12, 0);
        lv_obj_add_event_cb(s_health_confirm[i], on_health_confirm, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    }
    s_health_save = button(body, "Save snapshot", C_SURFACE2, C_TEXT, on_health_save, NULL);
    s_health_drip = button(body, "Record continued dripping", C_SURFACE2, C_TEXT, on_health_save, (void *)1);
    s_health_dry = button(body, "Record no further flow", C_SURFACE2, C_TEXT, on_health_save, (void *)2);
    lv_obj_set_size(s_health_save, LV_PCT(100), 64);
    lv_obj_set_size(s_health_drip, LV_PCT(100), 64);
    lv_obj_set_size(s_health_dry, LV_PCT(100), 64);
    s_health_limits = health_paragraph(body, "");
    lv_obj_t *source = button(body, "Procedure source (QR)", C_SURFACE2, C_TEXT, on_health_source, NULL);
    lv_obj_set_size(source, LV_PCT(100), 64);
    health_paragraph(body, "Mixture, oxygen-sensor, warm-up and ABS checks await AXK/controller-specific verification. Basic Settings and actuator tests are outside these passive checks.");
    health_paragraph(body, "Last saved snapshot on this device (replaced when you save this check again):");
    s_health_saved = health_paragraph(body, "");
    health_reset();
    lv_timer_create(health_timer, 1000, NULL);
}

/* ---- Wi-Fi and the wiki viewer ------------------------------------------------ */

/*
 * Wi-Fi only serves the Ross-Tech wiki viewer (net.c). The home screen shows
 * its state on a small button that opens the Wi-Fi screen.
 */

static lv_obj_t *s_wifi, *s_wifi_status, *s_wifi_list, *s_wifi_forget;
static lv_obj_t *s_pw_layer, *s_pw_ta;
static char s_pw_ssid[33];
static lv_obj_t *s_wiki, *s_wiki_title, *s_wiki_status, *s_wiki_body;
static lv_obj_t *s_wiki_return;
static int s_wiki_dtc = -1;
static char s_net_detail[48];

static uint32_t net_color(net_state_t st)
{
    return st == NET_CONNECTED ? C_OK : st == NET_CONNECTING ? C_WARN : st == NET_FAILED ? C_ERR : C_MUTED;
}

static void net_show_state(void)
{
    net_state_t st = net_state();
    lv_obj_set_style_text_color(lv_obj_get_child(s_wifi_icon, 0), col(net_color(st)), 0);
    const char *ssid = net_saved_ssid();
    switch (st) {
    case NET_STARTING:    lv_label_set_text(s_wifi_status, "Starting Wi-Fi..."); break;
    case NET_UNAVAILABLE: lv_label_set_text(s_wifi_status, "Wi-Fi isn't available: the ESP32-C6 didn't answer."); break;
    case NET_OFF:         lv_label_set_text(s_wifi_status, "Not connected. Tap Scan and pick your network."); break;
    case NET_CONNECTING:  label_printf(s_wifi_status, "%s  " LV_SYMBOL_BULLET "  %s", ssid, s_net_detail); break;
    case NET_CONNECTED:   label_printf(s_wifi_status, LV_SYMBOL_OK "  Connected to %s  " LV_SYMBOL_BULLET "  %s", ssid, s_net_detail); break;
    case NET_FAILED:      label_printf(s_wifi_status, LV_SYMBOL_WARNING "  %s: %s Tap it below to try again.", ssid, s_net_detail); break;
    }
    lv_obj_set_hidden(s_wifi_forget, ssid[0] == '\0');
}

static void on_wifi_open(lv_event_t *e)
{
    (void)e;
    net_show_state();
    lv_screen_load_anim(s_wifi, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(s_wifi_list);
    text(s_wifi_list, &lv_font_montserrat_20, C_MUTED, "Scanning...");
    net_scan();
}

static void on_wifi_forget(lv_event_t *e)
{
    (void)e;
    net_forget();
}

static void pw_close(void)
{
    if (s_pw_layer) {
        lv_obj_delete(s_pw_layer);
        s_pw_layer = NULL;
    }
}

static void pw_connect(void)
{
    net_connect(s_pw_ssid, lv_textarea_get_text(s_pw_ta));
    pw_close();
}

static void on_pw_button(lv_event_t *e)
{
    if (lv_event_get_user_data(e))
        pw_connect();
    else
        pw_close();
}

static void on_pw_keyboard(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_READY)
        pw_connect();
    else if (lv_event_get_code(e) == LV_EVENT_CANCEL)
        pw_close();
}

static void on_pw_show(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    lv_textarea_set_password_mode(s_pw_ta, !lv_obj_has_state(cb, LV_STATE_CHECKED));
}

/* Password entry over the Wi-Fi screen, with the on-screen keyboard. */
static void ask_password(const char *ssid)
{
    pw_close();
    snprintf(s_pw_ssid, sizeof(s_pw_ssid), "%s", ssid);
    s_pw_layer = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pw_layer);
    lv_obj_set_size(s_pw_layer, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_pw_layer, col(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pw_layer, LV_OPA_70, 0);
    lv_obj_set_clickable(s_pw_layer, true);

    lv_obj_t *panel = surface(s_pw_layer, C_SURFACE, 20);
    lv_obj_set_size(panel, 736, LV_SIZE_CONTENT);
    lv_obj_set_pos(panel, 32, 200);
    lv_obj_set_style_pad_all(panel, 24, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, 16, 0);
    label_printf(text(panel, &lv_font_montserrat_28, C_TEXT, ""), "Password for %s", ssid);

    s_pw_ta = lv_textarea_create(panel);
    lv_obj_set_width(s_pw_ta, LV_PCT(100));
    lv_textarea_set_one_line(s_pw_ta, true);
    lv_textarea_set_password_mode(s_pw_ta, true);
    lv_textarea_set_max_length(s_pw_ta, 64);
    lv_obj_set_style_text_font(s_pw_ta, &lv_font_montserrat_24, 0);

    lv_obj_t *show = lv_checkbox_create(panel);
    lv_checkbox_set_text(show, "Show password");
    lv_obj_add_event_cb(show, on_pw_show, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *buttons = row(panel, 16);
    lv_obj_t *cancel = button(buttons, "Cancel", C_SURFACE2, C_TEXT, on_pw_button, NULL);
    lv_obj_set_size(cancel, 200, 64);
    lv_obj_t *go = button(buttons, LV_SYMBOL_WIFI "  Connect", C_ACCENT, C_TEXT, on_pw_button, (void *)1);
    lv_obj_set_size(go, 260, 64);

    lv_obj_t *kb = lv_keyboard_create(s_pw_layer);
    lv_obj_set_size(kb, SCREEN_W, 440);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_pw_ta);
    lv_obj_add_event_cb(kb, on_pw_keyboard, LV_EVENT_ALL, NULL);
}

static void on_network(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_current_target(e);
    const char *ssid = lv_label_get_text(lv_obj_get_child(b, 0));
    if (lv_obj_get_user_data(b))
        ask_password(ssid);
    else
        net_connect(ssid, "");
}

static void show_networks(const net_event_t *ev)
{
    lv_obj_clean(s_wifi_list);
    if (ev->count < 0) {
        text(s_wifi_list, &lv_font_montserrat_20, C_WARN, "Scan failed. Try again in a moment.");
        return;
    }
    if (ev->count == 0) {
        text(s_wifi_list, &lv_font_montserrat_20, C_MUTED, "No networks found.");
        return;
    }
    for (int i = 0; i < ev->count; i++) {
        const net_ap_t *ap = &ev->aps[i];
        lv_obj_t *b = button(s_wifi_list, NULL, C_SURFACE, C_TEXT, on_network, NULL);
        lv_obj_set_size(b, LV_PCT(100), 72);
        lv_obj_set_style_radius(b, 16, 0);
        lv_obj_set_user_data(b, ap->secure ? (void *)1 : NULL);
        lv_obj_t *name = text(b, &lv_font_montserrat_24, C_TEXT, ap->ssid);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_set_width(name, 520);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        const char *bars = ap->rssi > -60 ? "Strong" : ap->rssi > -75 ? "Good" : "Weak";
        lv_obj_t *info = text(b, &lv_font_montserrat_16, C_MUTED, "");
        label_printf(info, "%s  " LV_SYMBOL_BULLET "  %s", ap->secure ? "Password" : "Open", bars);
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void build_wifi(void)
{
    s_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wifi, col(C_BG), 0);
    lv_obj_set_scrollable(s_wifi, false);

    lv_obj_t *back = round_button(s_wifi, LV_SYMBOL_LEFT, go_home);
    lv_obj_set_style_bg_color(back, col(C_SURFACE), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_pos(back, 24, 24);
    lv_obj_set_pos(text(s_wifi, &lv_font_montserrat_36, C_TEXT, "Wi-Fi"), 108, 32);

    lv_obj_t *note = text(s_wifi, &lv_font_montserrat_16, C_MUTED,
                          "Used only to read fault code pages from the Ross-Tech wiki. "
                          "Diagnostics never need it.");
    lv_obj_set_width(note, 736);
    lv_obj_set_pos(note, 32, 104);
    s_wifi_status = text(s_wifi, &lv_font_montserrat_20, C_TEXT, "");
    lv_obj_set_width(s_wifi_status, 736);
    lv_label_set_long_mode(s_wifi_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_wifi_status, 32, 150);

    lv_obj_t *scan = button(s_wifi, LV_SYMBOL_REFRESH "  Scan", C_ACCENT, C_TEXT, on_wifi_scan, NULL);
    lv_obj_set_size(scan, 360, 72);
    lv_obj_set_pos(scan, 32, 220);
    s_wifi_forget = button(s_wifi, LV_SYMBOL_TRASH "  Forget network", C_SURFACE, C_ERR, on_wifi_forget, NULL);
    lv_obj_set_size(s_wifi_forget, 360, 72);
    lv_obj_set_pos(s_wifi_forget, 408, 220);

    s_wifi_list = column(s_wifi, 12);
    lv_obj_set_size(s_wifi_list, 736, SCREEN_H - 320 - 24);
    lv_obj_set_pos(s_wifi_list, 32, 320);
    lv_obj_set_scrollable(s_wifi_list, true);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
}

/* ---- wiki viewer ---- */

static void on_wiki_back(lv_event_t *e)
{
    (void)e;
    s_wiki_dtc = -1;
    lv_screen_load_anim(s_wiki_return ? s_wiki_return : s_home, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 220, 0, false);
}

static void open_wiki(int dtc)
{
    char pcode[8] = "";
    s_wiki_return = lv_screen_active() == s_wiki ? s_wiki_return : lv_screen_active();
    s_wiki_dtc = dtc;
    if (kw_fault_pcode(dtc, pcode, sizeof(pcode)))
        label_printf(s_wiki_title, "%05d  " LV_SYMBOL_BULLET "  %s", dtc, pcode);
    else
        label_printf(s_wiki_title, "%05d", dtc);
    lv_obj_clean(s_wiki_body);
    lv_label_set_text(s_wiki_status, net_state() == NET_CONNECTED ? "Loading from wiki.ross-tech.com..." : "");
    lv_screen_load_anim(s_wiki, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
    net_fetch_wiki(dtc);
}

static void on_read_wiki(lv_event_t *e)
{
    lv_obj_t *mbox = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target(e)));
    int dtc = (int)(intptr_t)lv_event_get_user_data(e);
    lv_msgbox_close_async(mbox);
    open_wiki(dtc);
}

/* Lines from kw_wikitext_render: "= " title, "# " heading, "* "/"** " list, "  " text. */
static void show_page(const net_event_t *ev)
{
    if (ev->dtc != s_wiki_dtc)
        return;
    lv_obj_clean(s_wiki_body);
    if (!ev->ok) {
        lv_obj_set_style_text_color(s_wiki_status, col(C_WARN), 0);
        lv_label_set_text(s_wiki_status, ev->text);
        return;
    }
    lv_obj_set_style_text_color(s_wiki_status, col(C_MUTED), 0);
    lv_label_set_text(s_wiki_status, "From the Ross-Tech wiki (community-written; check against the repair manual).");

    static char line[1024], shown[1040];
    for (const char *p = ev->text; *p;) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof(line))
            n = sizeof(line) - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        p = nl ? nl + 1 : p + n;

        lv_obj_t *l;
        if (strncmp(line, "= ", 2) == 0) {
            l = text(s_wiki_body, &lv_font_montserrat_28, C_TEXT, line + 2);
        } else if (strncmp(line, "# ", 2) == 0) {
            l = text(s_wiki_body, &lv_font_montserrat_20, C_ACCENT, line + 2);
            lv_obj_set_style_pad_top(l, 14, 0);
        } else if (strncmp(line, "** ", 3) == 0) {
            snprintf(shown, sizeof(shown), "      -  %s", line + 3);
            l = text(s_wiki_body, &lv_font_montserrat_20, C_TEXT, shown);
        } else if (strncmp(line, "* ", 2) == 0) {
            snprintf(shown, sizeof(shown), LV_SYMBOL_BULLET "  %s", line + 2);
            l = text(s_wiki_body, &lv_font_montserrat_20, C_TEXT, shown);
        } else {
            l = text(s_wiki_body, &lv_font_montserrat_20, C_MUTED, line[0] == ' ' ? line + 2 : line);
        }
        lv_obj_set_width(l, LV_PCT(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    }
}

static void build_wiki(void)
{
    s_wiki = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wiki, col(C_BG), 0);
    lv_obj_set_scrollable(s_wiki, false);

    lv_obj_t *back = round_button(s_wiki, LV_SYMBOL_LEFT, on_wiki_back);
    lv_obj_set_style_bg_color(back, col(C_SURFACE), 0);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_pos(back, 24, 24);
    s_wiki_title = text(s_wiki, &lv_font_montserrat_36, C_TEXT, "");
    lv_obj_set_pos(s_wiki_title, 108, 32);
    s_wiki_status = text(s_wiki, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_width(s_wiki_status, 736);
    lv_label_set_long_mode(s_wiki_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_wiki_status, 32, 104);

    s_wiki_body = column(s_wiki, 10);
    lv_obj_set_size(s_wiki_body, 736, SCREEN_H - 160 - 24);
    lv_obj_set_pos(s_wiki_body, 32, 160);
    lv_obj_set_scrollable(s_wiki_body, true);
    lv_obj_set_scroll_dir(s_wiki_body, LV_DIR_VER);
}

void gui_on_net_event(const net_event_t *ev)
{
    if (!board_lvgl_lock())
        return;
    switch (ev->type) {
    case NET_EV_STATE:
        snprintf(s_net_detail, sizeof(s_net_detail), "%s", ev->detail ? ev->detail : "");
        net_show_state();
        break;
    case NET_EV_SCAN:
        show_networks(ev);
        break;
    case NET_EV_PAGE:
        show_page(ev);
        break;
    }
    board_lvgl_unlock();
}

/* ---- results -------------------------------------------------------------- */

static void on_fault_close(lv_event_t *e)
{
    lv_msgbox_close_async(lv_event_get_user_data(e));
}

/* Everything a KW1281 controller stores about one fault: the code and one status byte. */
static void on_fault_card(lv_event_t *e)
{
    int packed = (int)(intptr_t)lv_event_get_user_data(e);
    int dtc = packed >> 8, status = packed & 0xFF;
    const char *desc = kw_fault_describe(dtc);
    const char *type = kw_fault_type(status);
    char title[24], pcode[8] = "", body[512];

    if (kw_fault_pcode(dtc, pcode, sizeof(pcode)))
        snprintf(title, sizeof(title), "%05d  %s", dtc, pcode);
    else
        snprintf(title, sizeof(title), "%05d", dtc);
    snprintf(body, sizeof(body),
             "%s\n\n"
             "Fault type:  %02d  %s\n"
             "Occurrence:  %s\n"
             "Status byte:  0x%02X (%02d-%02d)\n\n"
             "This controller uses KW1281, which stores only the code and its status byte. "
             "There is no mileage, occurrence count or freeze frame data to read. "
             "Note the code down before clearing.",
             desc ? desc : "No description in the fault table",
             status & 0x7F, type ? type : "None given",
             status & 0x80 ? "Intermittent: the fault came and went" : "Static: the controller saw it continuously",
             status, status & 0x7F, (status >> 7) * 10);

    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_width(mbox, 720);
    lv_msgbox_add_title(mbox, title);
    lv_msgbox_add_text(mbox, body);

    /* read the page here over Wi-Fi, or scan the QR code to open it on a phone */
    char url[96];
    kw_fault_wiki_url(dtc, url, sizeof(url));
    lv_obj_t *link = lv_obj_create(lv_msgbox_get_content(mbox));
    lv_obj_remove_style_all(link);
    lv_obj_set_size(link, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(link, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(link, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(link, 20, 0);
    lv_obj_set_style_pad_top(link, 16, 0);
    lv_obj_t *qr = lv_qrcode_create(link);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, url, strlen(url));
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 10, 0);
    lv_obj_t *hint = lv_label_create(link);
    lv_label_set_text(hint, "Read on Ross-Tech shows this code's wiki page here (needs Wi-Fi), "
                            "or scan to open it on your phone. Not every code has a page.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(hint, 1);

    lv_obj_t *wiki = lv_msgbox_add_footer_button(mbox, "Read on Ross-Tech");
    lv_obj_add_event_cb(wiki, on_read_wiki, LV_EVENT_CLICKED, (void *)(intptr_t)dtc);
    lv_obj_t *ok = lv_msgbox_add_footer_button(mbox, "Done");
    lv_obj_add_event_cb(ok, on_fault_close, LV_EVENT_CLICKED, mbox);
}

static void add_fault_card(const FaultCode *fc)
{
    char code[8], status[32], pcode[8] = "";
    bool intermittent = fc->status & 0x80;
    const char *desc = kw_fault_describe(fc->dtc);
    const char *type = kw_fault_type(fc->status);

    lv_obj_t *card = surface(s_fault_list, C_SURFACE, 18);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_pad_all(card, 20, 0);
    lv_obj_set_style_pad_left(card, 28, 0);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(card, 6, 0);
    lv_obj_set_style_border_color(card, col(intermittent ? C_WARN : C_ERR), 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);

    lv_obj_t *top = row(card, 14);
    snprintf(code, sizeof(code), "%05d", fc->dtc);
    text(top, &lv_font_montserrat_28, C_TEXT, code);
    if (kw_fault_pcode(fc->dtc, pcode, sizeof(pcode))) {
        lv_obj_t *chip = surface(top, C_SURFACE2, 10);
        lv_obj_set_style_pad_hor(chip, 10, 0);
        lv_obj_set_style_pad_ver(chip, 4, 0);
        text(chip, &lv_font_montserrat_16, C_TEXT, pcode);
    }
    lv_obj_t *spacer = plain(top);
    lv_obj_set_flex_grow(spacer, 1);
    snprintf(status, sizeof(status), "%02d-%02d%s", fc->status & 0x7F, (fc->status >> 7) * 10,
             intermittent ? "  intermittent" : "");
    text(top, &lv_font_montserrat_16, intermittent ? C_WARN : C_MUTED, status);

    lv_obj_t *d = text(card, &lv_font_montserrat_20, desc ? C_TEXT : C_MUTED,
                       desc ? desc : "No description in the fault table");
    lv_obj_set_width(d, LV_PCT(100));
    lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
    if (type)
        text(card, &lv_font_montserrat_20, intermittent ? C_WARN : C_ERR, type);

    lv_obj_set_clickable(card, true);
    lv_obj_add_event_cb(card, on_fault_card, LV_EVENT_CLICKED, (void *)(intptr_t)(fc->dtc << 8 | fc->status));
    lv_obj_fade_in(card, 250, 0);
}

static void show_faults(const kl_event_t *ev)
{
    for (int i = 0; i < s_module_count; i++) {
        if (s_modules[i].address == ev->address) {
            s_modules[i].error = ev->ok ? 0 : ev->err;
            s_modules[i].faults = ev->ok ? ev->count : -1;
            set_card_status(&s_modules[i]);
        }
    }
    if (s_current < 0 || s_modules[s_current].address != ev->address)
        return;
    lv_obj_clean(s_fault_list);
    if (!ev->ok) {
        lv_label_set_text_fmt(s_fault_status, LV_SYMBOL_CLOSE "  %s",
                              ev->err == KW_ENOWAKE ? "No response. Check ignition and address, then tap Read faults to retry."
                              : ev->err == KW_EKWP2000 ? "KWP2000 is not supported."
                              : kw_strerror(ev->err));
        return;
    }
    if (ev->count == 0) {
        lv_label_set_text(s_fault_status, ev->op == KL_CLEAR ? "Cleared." : "");
        lv_obj_t *empty = surface(s_fault_list, C_SURFACE, 18);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_all(empty, 32, 0);
        lv_obj_set_flex_flow(empty, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(empty, 10, 0);
        text(empty, &lv_font_montserrat_48, C_OK, LV_SYMBOL_OK);
        text(empty, &lv_font_montserrat_24, C_TEXT, "No fault codes stored");
        return;
    }
    lv_label_set_text_fmt(s_fault_status, "%s%d fault code%s", ev->op == KL_CLEAR ? "Still present: " : "",
                          ev->count, ev->count == 1 ? "" : "s");
    for (int i = 0; i < ev->count; i++)
        add_fault_card(&ev->faults[i]);
}

static void show_values(const kl_event_t *ev)
{
    if (s_current < 0 || s_modules[s_current].address != ev->address)
        return; /* a stream for a controller that is no longer on screen */
    if (!ev->ok) {
        lv_label_set_text(s_group_status, ev->err == KW_ENAK ? "This controller has no such group."
                                                             : kw_strerror(ev->err));
        for (int i = 0; i < MAX_VALUES; i++)
            lv_obj_set_hidden(s_value_tiles[i], true);
        return;
    }
    lv_label_set_text_fmt(s_group_status, "Group %d  " LV_SYMBOL_BULLET "  %s%s", s_group,
                          s_live ? "live, tap a value to pin it" : "tap Live to keep updating",
                          kw_labels_standard(s_modules[s_current].ident, s_group)
                              ? "  " LV_SYMBOL_BULLET "  standard VW labels" : "");
    for (int i = 0; i < MAX_VALUES; i++) {
        if (i < ev->count) {
            lv_label_set_text(s_value_labels[i], ev->values[i]);
            lv_label_set_text_fmt(s_value_names[i], "%d  " LV_SYMBOL_BULLET "  %s", i + 1, ev->labels[i]);
            snprintf(s_value_label_text[i], sizeof(s_value_label_text[i]), "%s", ev->labels[i]);
            lv_obj_set_hidden(s_value_tiles[i], false);
        } else {
            lv_obj_set_hidden(s_value_tiles[i], true);
        }
    }
}

static void scan_finished(void)
{
    s_scanning = false;
    s_scanned = true;
    lv_label_set_text(s_scan_label, LV_SYMBOL_REFRESH "  Scan again");
    lv_obj_set_hidden(s_progress, true);
    if (found_count() == 0) {
        lv_label_set_text(s_scan_status, "No controllers answered. Is the ignition on?");
        return;
    }
    lv_label_set_text_fmt(s_scan_status, "Scan complete%s", s_demo ? "  " LV_SYMBOL_BULLET "  demo car, not your car" : "");
    if (!s_car_manual && kw_car_best(&s_votes) == KW_CAR_UNKNOWN)
        show_car(KW_CAR_UNKNOWN, s_car != KW_CAR_UNKNOWN);
}

void gui_on_kline_event(const kl_event_t *ev)
{
    if (!board_lvgl_lock())
        return;

    switch (ev->type) {
    case KL_EV_CABLE:
        s_cable = ev->ok;
        s_demo = ev->demo;
        update_pill();
        if (!s_scanning)
            lv_label_set_text(s_scan_status, s_cable ? "" : "Plug the KKL cable into the P4's second USB port");
        break;
    case KL_EV_PROGRESS: {
        const char *name = kw_module_name(ev->address);
        lv_bar_set_value(s_progress, (ev->count + 1) * 100 / ev->total, LV_ANIM_ON);
        lv_label_set_text_fmt(s_scan_status, "Probing %02X%s%s", ev->address, name ? "  " LV_SYMBOL_BULLET "  " : "",
                              name ? name : "");
        break;
    }
    case KL_EV_MODULE: {
        int idx = ensure_module(ev->address);
        if (idx >= 0) {
            module_t *m = &s_modules[idx];
            m->protocol = ev->protocol;
            m->supported = ev->ok;
            m->faults = -1;
            m->error = 0;
            snprintf(m->ident, sizeof(m->ident), "%s", ev->ident);
            lv_label_set_text(m->ident_label, m->ident);
            set_card_status(m);
            lv_label_set_text_fmt(s_count_label, "%d found", found_count());
            if (s_current == idx) {
                lv_label_set_text_fmt(s_detail_sub, "Address %02X  " LV_SYMBOL_BULLET "  KW%d", m->address, m->protocol);
                lv_label_set_text(s_detail_ident, m->ident);
            }
            if (ev->ok)
                kw_car_vote(&s_votes, ev->ident);
            KwCar best = kw_car_best(&s_votes);
            if (!s_car_manual && best != KW_CAR_UNKNOWN && best != s_car) {
                show_car(best, true);
                save_car();
            }
        }
        break;
    }
    case KL_EV_FAULTS:
        show_faults(ev);
        break;
    case KL_EV_GROUP:
        show_values(ev);
        break;
    case KL_EV_DASH:
        dash_apply(ev);
        break;
    case KL_EV_HEALTH:
        if (s_health_running && ev->health) {
            s_health_frame = *ev->health;
            s_health_have = true;
            s_health_received = esp_timer_get_time() / 1e6;
            health_evaluate();
        }
        break;
    case KL_EV_DONE:
        s_busy = false;
        if (ev->op == KL_HEALTH) {
            s_health_running = false;
            lv_label_set_text(s_health_run_label, "Start");
            lv_obj_set_state(s_health_picker, LV_STATE_DISABLED, false);
            for (int i = 0; i < 4; i++) lv_obj_set_state(s_health_confirm[i], LV_STATE_DISABLED, true);
            health_evaluate();
            if (!s_health_have) lv_label_set_text(s_health_status, "Stopped. Start again to collect live readings.");
        }
        if (ev->op == KL_SCAN || ev->op == KL_DEMO)
            scan_finished();
        if (ev->op == KL_DASH) {
            dash_set_running(false);
            if (s_dash_restart) {
                s_dash_restart = false;
                dash_start();
            } else if (strncmp(lv_label_get_text(s_dash_status), "No cable", 8) != 0) {
                lv_label_set_text(s_dash_status, "Stopped.");
            }
        }
        if (ev->op == KL_GROUP_LIVE) {
            s_live = false;
            lv_label_set_text(s_live_label, LV_SYMBOL_PLAY "  Live");
            if (s_restart_live) {
                s_restart_live = false;
                toggle_live();
            }
        }
        if (s_group_pending && s_current >= 0) {
            s_group_pending = false;
            read_group_once();
        }
        break;
    }

    board_lvgl_unlock();
}

void gui_build(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(disp, col(C_ACCENT), col(C_ERR), true, &lv_font_montserrat_20);
    lv_display_set_theme(disp, theme);

    dash_load();
    build_home();
    build_detail();
    build_dash();
    build_health();
    build_wifi();
    build_wiki();
    build_credits();
    update_pill();
    show_car(load_car(), false);
}

/* Commands from the serial debug console (see debug_console.c). */
void gui_debug(const char *cmd)
{
    if (!board_lvgl_lock())
        return;
    if (strncmp(cmd, "demo ", 5) == 0 && !s_busy) {
        if (strstr(cmd, "eurovan"))
            start_demo(FAKE_CAR_EUROVAN);
        else if (strstr(cmd, "harlequin"))
            start_demo(FAKE_CAR_HARLEQUIN);
        else
            start_demo(FAKE_CAR_GOLF4);
    } else if (strncmp(cmd, "module ", 7) == 0 && !s_busy) {
        char *end;
        long address = strtol(cmd + 7, &end, 16);
        if (*end == '\0' && address >= 1 && address <= 0x7F)
            open_module_index(ensure_module((uint8_t)address));
    } else if (strcmp(cmd, "modules") == 0) {
        on_more_modules(NULL);
    } else if (strcmp(cmd, "health") == 0) {
        on_health_open(NULL);
    } else if (strncmp(cmd, "health check ", 13) == 0 && !s_busy) {
        int check = atoi(cmd + 13);
        if (check >= 0 && check < KW_HEALTH_COUNT) {
            lv_dropdown_set_selected(s_health_picker, check);
            on_health_pick(NULL);
            on_health_open(NULL);
        }
    } else if (strcmp(cmd, "health start") == 0 || strcmp(cmd, "health stop") == 0) {
        if ((strcmp(cmd, "health start") == 0) != s_health_running) on_health_run(NULL);
    } else if (strncmp(cmd, "open ", 5) == 0) {
        open_module_index(cmd[5] - '0');
    } else if (strcmp(cmd, "back") == 0) {
        on_back(NULL);
    } else if (strcmp(cmd, "faults") == 0) {
        show_tab(false);
        read_faults();
    } else if (strcmp(cmd, "live") == 0) {
        show_tab(true);
        toggle_live();
    } else if (strcmp(cmd, "plus") == 0 || strcmp(cmd, "minus") == 0) {
        step_group(cmd[0] == 'p' ? 1 : -1);
    } else if (strcmp(cmd, "chooser") == 0) {
        on_demo(NULL);
    } else if (strcmp(cmd, "credits") == 0) {
        on_credits(NULL);
    } else if (strcmp(cmd, "wifi") == 0) {
        on_wifi_open(NULL);
    } else if (strcmp(cmd, "wifi scan") == 0) {
        on_wifi_open(NULL);
        on_wifi_scan(NULL);
    } else if (strncmp(cmd, "wiki ", 5) == 0) {
        open_wiki(atoi(cmd + 5));
    } else if (strcmp(cmd, "dash") == 0) {
        on_dash_open(NULL);
    } else if (strcmp(cmd, "dash start") == 0) {
        on_dash_open(NULL);
        dash_start();
    } else if (strcmp(cmd, "dash stop") == 0) {
        if (s_dash_running)
            kline_cancel();
    } else if (strcmp(cmd, "home") == 0) {
        lv_screen_load(s_home);
    }
    board_lvgl_unlock();
}
