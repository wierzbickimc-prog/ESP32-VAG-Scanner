/*
 * Display bring-up for the Guition JC8012P4A1C: 800x1280 JD9365 MIPI-DSI
 * panel (2 lanes) and GSL3680 touch on I2C. Pins, timing and orientation
 * follow https://github.com/chipguyhere/JC8012P4A1C_display; LVGL runs via
 * esp_lvgl_port.
 */
#include "board_display.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_jd9365.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "gsl3680_fw.h"

static const char *TAG = "display";

#define LCD_H_RES 800
#define LCD_V_RES 1280
#define LCD_RST 27
#define LCD_BACKLIGHT 23
#define TP_SDA 7
#define TP_SCL 8
#define TP_RST 22
#define TP_INT 21
#define DSI_PHY_LDO_CHAN 3
#define DSI_PHY_LDO_MV 2500

static esp_lcd_panel_handle_t board_panel_create(esp_lcd_panel_io_handle_t *io_out)
{
    static esp_ldo_channel_handle_t phy_ldo;
    const esp_ldo_channel_config_t ldo_cfg = { .chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV };
    if (esp_ldo_acquire_channel(&ldo_cfg, &phy_ldo) != ESP_OK)
        return NULL;

    const gpio_config_t bl_cfg = { .pin_bit_mask = 1ULL << LCD_BACKLIGHT, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl_cfg);
    gpio_set_level(LCD_BACKLIGHT, 0);

    esp_lcd_dsi_bus_handle_t bus;
    const esp_lcd_dsi_bus_config_t bus_cfg = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK)
        return NULL;

    const esp_lcd_dbi_io_config_t dbi_cfg = JD9365_PANEL_IO_DBI_CONFIG();
    if (esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, io_out) != ESP_OK)
        return NULL;

    static const esp_lcd_dpi_panel_config_t dpi_cfg = JD9365_800_1280_PANEL_60HZ_DPI_CONFIG(LCD_COLOR_PIXEL_FORMAT_RGB565);
    jd9365_vendor_config_t vendor = {
        .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi_cfg, .lane_num = 2 },
    };
    const esp_lcd_panel_dev_config_t dev_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    esp_lcd_panel_handle_t panel;
    if (esp_lcd_new_panel_jd9365(*io_out, &dev_cfg, &panel) != ESP_OK)
        return NULL;
    if (esp_lcd_panel_reset(panel) != ESP_OK || esp_lcd_panel_init(panel) != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return NULL;
    }
    /* Base scan orientation used by the reference driver (MY|MX). */
    esp_lcd_panel_io_tx_param(*io_out, 0xE0, (uint8_t[]){ 0x00 }, 1);
    esp_lcd_panel_io_tx_param(*io_out, 0x36, (uint8_t[]){ 0xC0 }, 1);

    gpio_set_level(LCD_BACKLIGHT, 1);
    return panel;
}

static esp_lcd_touch_handle_t board_touch_create(void)
{
    i2c_master_bus_handle_t i2c;
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = -1,
        .sda_io_num = TP_SDA,
        .scl_io_num = TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&i2c_cfg, &i2c) != ESP_OK)
        return NULL;

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_GSL3680_CONFIG();
    tp_io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(i2c, &tp_io_cfg, &tp_io) != ESP_OK)
        return NULL;

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = TP_RST,
        .int_gpio_num = TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        /* With the panel at MADCTL 0xC0 the touch reports both axes inverted (measured on hardware). */
        .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 0 },
    };
    esp_lcd_touch_handle_t tp;
    if (gsl3680_touch_init_local(tp_io, &tp_cfg, &tp) != ESP_OK)
        return NULL;
    return tp;
}

bool board_display_init(void)
{
    esp_lcd_panel_io_handle_t panel_io;
    esp_lcd_panel_handle_t panel = board_panel_create(&panel_io);
    if (!panel) {
        ESP_LOGW(TAG, "panel bring-up failed: running headless");
        return false;
    }

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port_cfg) != ESP_OK)
        return false;

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = panel_io,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * 100,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = { .buff_spiram = true },
    };
    const lvgl_port_display_dsi_cfg_t dsi_cfg = { .flags = { .avoid_tearing = false } };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
    if (!disp)
        return false;

    esp_lcd_touch_handle_t touch = board_touch_create();
    if (touch) {
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = touch };
        lvgl_port_add_touch(&touch_cfg);
    } else {
        ESP_LOGW(TAG, "touch init failed: view only");
    }
    return true;
}

bool board_lvgl_lock(void)
{
    return lvgl_port_lock(0);
}

void board_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
