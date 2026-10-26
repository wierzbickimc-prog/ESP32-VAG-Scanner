/*
 * LVGL 9 UI for the 800x1280 portrait panel. Dark theme; the detected car's
 * photo fills the top of the home screen, controllers sit in a card grid
 * below, and each controller opens a detail screen with Faults / Live data.
 */
#include "gui.h"

#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "board_display.h"
#include "car_profile.h"
#include "fault_table.h"
#include "labels.h"
#include "lvgl.h"
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
    char ident[KW_IDENT_MAX];
    lv_obj_t *status;
} module_t;

static module_t s_modules[MAX_MODULES];
static int s_module_count;
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
static lv_obj_t *s_count_label, *s_grid, *s_grid_empty;
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
    } else if (m->faults < 0) {
        lv_label_set_text(m->status, "Tap to read faults");
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

    lv_obj_t *ident = text(card, &lv_font_montserrat_16, C_MUTED, m->supported ? m->ident : "");
    lv_obj_set_pos(ident, 76, 36);
    lv_obj_set_size(ident, 248, 22);
    lv_label_set_long_mode(ident, LV_LABEL_LONG_DOT);

    m->status = text(card, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(m->status, LV_ALIGN_BOTTOM_LEFT, 76, 0);
    set_card_status(m);

    if (!m->supported) {
        lv_obj_add_state(card, LV_STATE_DISABLED);
        lv_obj_set_style_opa(card, LV_OPA_60, LV_STATE_DISABLED);
    }
    lv_obj_set_hidden(s_grid_empty, true);
    lv_obj_fade_in(card, 300, 0);
}

static void clear_modules(void)
{
    s_module_count = 0;
    lv_obj_clean(s_grid);
    lv_label_set_text(s_count_label, "");
    lv_obj_set_hidden(s_grid_empty, false);
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

    s_hero = lv_image_create(s_home);
    lv_obj_set_pos(s_hero, 0, 0);

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
    s_pill_dot = surface(pill, C_MUTED, LV_RADIUS_CIRCLE);
    lv_obj_set_size(s_pill_dot, 12, 12);
    s_pill_label = text(pill, &lv_font_montserrat_16, C_TEXT, "");

    /* car name over the bottom of the photo */
    s_title = text(s_home, &lv_font_montserrat_48, C_TEXT, "");
    lv_obj_set_pos(s_title, 32, 520);
    lv_obj_set_clickable(s_title, true);
    lv_obj_set_ext_click_area(s_title, 16);
    lv_obj_add_event_cb(s_title, on_title, LV_EVENT_CLICKED, NULL);
    s_subtitle = text(s_home, &lv_font_montserrat_20, C_MUTED, "");
    lv_obj_set_pos(s_subtitle, 34, 582);

    /* scan, and demo beside it */
    lv_obj_t *scan = button(s_home, NULL, C_ACCENT, C_TEXT, on_scan, NULL);
    lv_obj_add_event_cb(scan, on_scan_hold, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_set_size(scan, 540, 96);
    lv_obj_set_pos(scan, 32, 636);
    lv_obj_set_style_radius(scan, 24, 0);
    s_scan_label = text(scan, &lv_font_montserrat_28, C_TEXT, LV_SYMBOL_REFRESH "  Scan car");
    lv_obj_center(s_scan_label);

    lv_obj_t *demo = button(s_home, LV_SYMBOL_PLAY "  Demo", C_SURFACE, C_WARN, on_demo, NULL);
    lv_obj_set_size(demo, 180, 96);
    lv_obj_set_pos(demo, 588, 636);
    lv_obj_set_style_radius(demo, 24, 0);
    lv_obj_set_style_border_color(demo, col(C_WARN), 0);
    lv_obj_set_style_border_width(demo, 2, 0);

    s_progress = lv_bar_create(s_home);
    lv_obj_set_size(s_progress, 736, 6);
    lv_obj_set_pos(s_progress, 32, 746);
    lv_obj_set_style_bg_color(s_progress, col(C_SURFACE2), 0);
    lv_obj_set_style_bg_color(s_progress, col(C_ACCENT), LV_PART_INDICATOR);
    lv_bar_set_range(s_progress, 0, 100);
    lv_obj_set_hidden(s_progress, true);

    s_scan_status = text(s_home, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_set_pos(s_scan_status, 34, 762);

    /* controllers */
    lv_obj_t *heading = text(s_home, &lv_font_montserrat_16, C_MUTED, "CONTROLLERS");
    lv_obj_set_style_text_letter_space(heading, 3, 0);
    lv_obj_set_pos(heading, 34, 808);
    s_count_label = text(s_home, &lv_font_montserrat_16, C_MUTED, "");
    lv_obj_align(s_count_label, LV_ALIGN_TOP_RIGHT, -34, 808);

    s_grid = plain(s_home);
    lv_obj_set_scrollable(s_grid, true);
    lv_obj_set_scroll_dir(s_grid, LV_DIR_VER);
    lv_obj_set_size(s_grid, 736, SCREEN_H - 842 - 16);
    lv_obj_set_pos(s_grid, 32, 842);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(s_grid, 16, 0);
    lv_obj_set_style_pad_column(s_grid, 16, 0);

    s_grid_empty = text(s_home, &lv_font_montserrat_20, C_MUTED,
                        "Ignition on, then tap Scan.\nNo car here? Tap Demo to explore.");
    lv_obj_set_style_text_align(s_grid_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(s_grid_empty, 8, 0);
    lv_obj_align(s_grid_empty, LV_ALIGN_TOP_MID, 0, 940);
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
    if (s_busy || s_current < 0)
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
    lv_label_set_text(s_fault_status, "Reading fault codes...");
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
    if (s_busy)
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
    if (s_busy)
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
        lv_obj_set_hidden(tile, true);
    }
    show_tab(false);
}

static void open_module_index(int idx)
{
    if (idx < 0 || idx >= s_module_count || !s_modules[idx].supported)
        return;
    module_t *m = &s_modules[idx];
    if (s_live) {
        s_restart_live = false;
        kline_cancel();
    }
    s_current = idx;
    lv_label_set_text(s_detail_name, module_name(m->address));
    lv_label_set_text_fmt(s_detail_sub, "Address %02X  " LV_SYMBOL_BULLET "  KW%d", m->address, m->protocol);
    lv_label_set_text(s_detail_ident, m->ident);
    lv_label_set_text(s_fault_status, "Tap Read faults to ask the controller.");
    lv_obj_clean(s_fault_list);
    lv_label_set_text(s_group_status, "Use - and + to pick a measuring group, then Live.");
    for (int i = 0; i < MAX_VALUES; i++)
        lv_obj_set_hidden(s_value_tiles[i], true);
    show_tab(false);
    lv_screen_load_anim(s_detail, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 220, 0, false);
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

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, on_fault_card, LV_EVENT_CLICKED, (void *)(intptr_t)(fc->dtc << 8 | fc->status));
    lv_obj_fade_in(card, 250, 0);
}

static void show_faults(const kl_event_t *ev)
{
    for (int i = 0; i < s_module_count; i++) {
        if (s_modules[i].address == ev->address && ev->ok) {
            s_modules[i].faults = ev->count;
            set_card_status(&s_modules[i]);
        }
    }
    if (s_current < 0 || s_modules[s_current].address != ev->address)
        return;
    lv_obj_clean(s_fault_list);
    if (!ev->ok) {
        lv_label_set_text_fmt(s_fault_status, LV_SYMBOL_CLOSE "  %s", kw_strerror(ev->err));
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
                          s_live ? "live" : "tap Live to keep updating",
                          kw_labels_standard(s_modules[s_current].ident, s_group)
                              ? "  " LV_SYMBOL_BULLET "  standard VW labels" : "");
    for (int i = 0; i < MAX_VALUES; i++) {
        if (i < ev->count) {
            lv_label_set_text(s_value_labels[i], ev->values[i]);
            lv_label_set_text_fmt(s_value_names[i], "%d  " LV_SYMBOL_BULLET "  %s", i + 1, ev->labels[i]);
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
    if (s_module_count == 0) {
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
    case KL_EV_MODULE:
        if (s_module_count < MAX_MODULES) {
            module_t *m = &s_modules[s_module_count];
            m->address = ev->address;
            m->protocol = ev->protocol;
            m->supported = ev->ok;
            m->faults = -1;
            snprintf(m->ident, sizeof(m->ident), "%s", ev->ident);
            add_card(s_module_count++);
            lv_label_set_text_fmt(s_count_label, "%d found", s_module_count);
            if (ev->ok)
                kw_car_vote(&s_votes, ev->ident);
            KwCar best = kw_car_best(&s_votes);
            if (!s_car_manual && best != KW_CAR_UNKNOWN && best != s_car) {
                show_car(best, true);
                save_car();
            }
        }
        break;
    case KL_EV_FAULTS:
        show_faults(ev);
        break;
    case KL_EV_GROUP:
        show_values(ev);
        break;
    case KL_EV_DONE:
        s_busy = false;
        if (ev->op == KL_SCAN || ev->op == KL_DEMO)
            scan_finished();
        if (ev->op == KL_GROUP_LIVE) {
            s_live = false;
            lv_label_set_text(s_live_label, LV_SYMBOL_PLAY "  Live");
            if (s_restart_live) {
                s_restart_live = false;
                toggle_live();
            }
        }
        if (ev->op == KL_GROUP && s_group_pending) {
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

    build_home();
    build_detail();
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
    } else if (strcmp(cmd, "home") == 0) {
        lv_screen_load(s_home);
    }
    board_lvgl_unlock();
}
