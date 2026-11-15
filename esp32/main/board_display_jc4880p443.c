/* Guition JC4880P443: 480x800 ST7701S MIPI-DSI and GT911 I2C touch.
 * Pins/timing/table: Guition-JC/Guition commit
 * 737d0c14001f82e79ae82a41257cf0a3c86bc101, jc4880p443 board definition.
 * Physical panel orientation and touch still require a test on this board.
 */
#include "board_display.h"
#include "board_config.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_gt911.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "panel_init.h"

static const char *TAG = "display";

static esp_err_t board_panel_create(esp_lcd_panel_io_handle_t *io, esp_lcd_panel_handle_t *panel)
{
    static esp_ldo_channel_handle_t phy_ldo;
    const esp_ldo_channel_config_t ldo = { .chan_id = 3, .voltage_mv = 2500 };
    ESP_RETURN_ON_ERROR(esp_ldo_acquire_channel(&ldo, &phy_ldo), TAG, "DSI power");
    const gpio_config_t backlight = { .pin_bit_mask = 1ULL << 23, .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(23, 0), TAG, "backlight off");

    esp_lcd_dsi_bus_handle_t bus;
    const esp_lcd_dsi_bus_config_t bus_cfg = {
        /* Zero lets ESP-IDF choose the clock for this chip revision. The old
         * MIPI_DSI_PHY_CLK_SRC_DEFAULT alias selects a legacy PLL and aborts on v3. */
        .bus_id = 0, .num_data_lanes = 2, .phy_clk_src = 0,
        .lane_bit_rate_mbps = 550,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_dsi_bus(&bus_cfg, &bus), TAG, "DSI bus");
    const esp_lcd_dbi_io_config_t dbi = ST7701_PANEL_IO_DBI_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_dbi(bus, &dbi, io), TAG, "panel IO");
    const esp_lcd_dpi_panel_config_t dpi = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = 34,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .num_fbs = 2,
        .video_timing = {
            .h_size = BOARD_WIDTH, .v_size = BOARD_HEIGHT,
            .hsync_pulse_width = 12, .hsync_back_porch = 42, .hsync_front_porch = 42,
            .vsync_pulse_width = 2, .vsync_back_porch = 8, .vsync_front_porch = 166,
        },
        .flags.use_dma2d = true,
    };
    st7701_vendor_config_t vendor = {
        .init_cmds = lcd_cmd,
        .init_cmds_size = sizeof(lcd_cmd) / sizeof(lcd_cmd[0]),
        .mipi_config = { .dsi_bus = bus, .dpi_config = &dpi },
        .flags.use_mipi_interface = true,
    };
    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = 5, .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 24, .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7701(*io, &dev, panel), TAG, "ST7701 create");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*panel), TAG, "ST7701 reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*panel), TAG, "ST7701 init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*panel, true), TAG, "ST7701 display on");
    return gpio_set_level(23, 1);
}

static esp_lcd_touch_handle_t board_touch_create(void)
{
    i2c_master_bus_handle_t bus;
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1, .sda_io_num = 7, .scl_io_num = 8,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK)
        return NULL;
    /* Guition leaves reset/interrupt unwired in its board definition. Probe both
     * GT911 strap addresses instead of driving an unconfirmed reset GPIO. */
    uint8_t address = 0;
    if (i2c_master_probe(bus, 0x5D, 100) == ESP_OK)
        address = 0x5D;
    else if (i2c_master_probe(bus, 0x14, 100) == ESP_OK)
        address = 0x14;
    if (!address) {
        ESP_LOGW(TAG, "GT911 did not answer at 0x5D or 0x14");
        i2c_del_master_bus(bus);
        return NULL;
    }
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = address;
    io_cfg.scl_speed_hz = 400000;
    if (esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io) != ESP_OK) {
        i2c_del_master_bus(bus);
        return NULL;
    }
    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_WIDTH, .y_max = BOARD_HEIGHT,
        .rst_gpio_num = -1, .int_gpio_num = -1,
    };
    esp_lcd_touch_handle_t touch;
    if (esp_lcd_touch_new_i2c_gt911(io, &cfg, &touch) != ESP_OK) {
        esp_lcd_panel_io_del(io);
        i2c_del_master_bus(bus);
        return NULL;
    }
    ESP_LOGI(TAG, "GT911 ready at 0x%02X", address);
    return touch;
}

bool board_display_init(void)
{
    ESP_LOGI(TAG, "%s, %dx%d", BOARD_NAME, BOARD_WIDTH, BOARD_HEIGHT);
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_handle_t panel;
    if (board_panel_create(&io, &panel) != ESP_OK)
        return false;
    const lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port) != ESP_OK)
        return false;
    const lvgl_port_display_cfg_t display = {
        .io_handle = io, .panel_handle = panel,
        .buffer_size = BOARD_WIDTH * 100, .double_buffer = true,
        .hres = BOARD_WIDTH, .vres = BOARD_HEIGHT,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags.buff_spiram = true,
    };
    const lvgl_port_display_dsi_cfg_t dsi = { .flags.avoid_tearing = false };
    lv_display_t *disp = lvgl_port_add_disp_dsi(&display, &dsi);
    if (!disp)
        return false;
    esp_lcd_touch_handle_t touch = board_touch_create();
    if (!touch) {
        ESP_LOGW(TAG, "touch init failed: view only");
    } else {
        const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = touch };
        if (!lvgl_port_add_touch(&touch_cfg))
            ESP_LOGW(TAG, "LVGL touch registration failed: view only");
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
