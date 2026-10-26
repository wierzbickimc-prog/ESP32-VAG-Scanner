/*
 * Hero images, built by assets/tools/make_assets.py as raw RGB565 and embedded
 * in flash. Each is copied once into aligned PSRAM, where LVGL can blit it.
 */
#include "assets.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "assets";

#define HERO_BYTES (HERO_W * HERO_H * 2)

extern const uint8_t hero_eurovan_start[] asm("_binary_hero_eurovan_rgb565_start");
extern const uint8_t hero_harlequin_start[] asm("_binary_hero_harlequin_rgb565_start");
extern const uint8_t hero_family_start[] asm("_binary_hero_family_rgb565_start");
extern const char credits_start[] asm("_binary_CREDITS_md_start");

typedef struct {
    const char *key;
    const uint8_t *flash;
    lv_image_dsc_t dsc;
    bool loaded;
} Hero;

static Hero s_heroes[] = {
    { .key = "eurovan", .flash = hero_eurovan_start },
    { .key = "harlequin", .flash = hero_harlequin_start },
    { .key = "family", .flash = hero_family_start },
};

const lv_image_dsc_t *assets_hero(const char *key)
{
    Hero *h = &s_heroes[2]; /* family */
    for (size_t i = 0; i < sizeof(s_heroes) / sizeof(s_heroes[0]); i++)
        if (strcmp(s_heroes[i].key, key) == 0)
            h = &s_heroes[i];

    if (!h->loaded) {
        uint8_t *buf = heap_caps_aligned_alloc(64, HERO_BYTES, MALLOC_CAP_SPIRAM);
        if (!buf) {
            ESP_LOGE(TAG, "no PSRAM for %s", h->key);
            buf = (uint8_t *)h->flash; /* draw straight from flash instead */
        } else {
            memcpy(buf, h->flash, HERO_BYTES);
        }
        h->dsc = (lv_image_dsc_t){
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_RGB565,
                .w = HERO_W,
                .h = HERO_H,
                .stride = HERO_W * 2,
            },
            .data_size = HERO_BYTES,
            .data = buf,
        };
        h->loaded = true;
    }
    return &h->dsc;
}

const char *assets_credits(void)
{
    static char text[2048];
    if (text[0])
        return text;

    /* CREDITS.md table rows: | name | author | licence | [title](url) | */
    size_t pos = 0;
    const char *line = credits_start;
    while (*line && pos < sizeof(text) - 160) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > 2 && line[0] == '|' && line[2] != '-' && strncmp(line, "| Image", 7) != 0) {
            char cells[4][96] = { { 0 } };
            int c = -1;
            size_t n = 0;
            for (size_t i = 0; i < len; i++) {
                if (line[i] == '|') {
                    if (c >= 0 && c < 4) {
                        while (n > 0 && cells[c][n - 1] == ' ')
                            n--;
                        cells[c][n] = '\0';
                    }
                    c++;
                    n = 0;
                } else if (c >= 0 && c < 4 && n < sizeof(cells[0]) - 1 && !(n == 0 && line[i] == ' ')) {
                    cells[c][n++] = line[i];
                }
            }
            pos += snprintf(text + pos, sizeof(text) - pos, "%s: %s, %s\n", cells[0], cells[1], cells[2]);
        }
        line = end ? end + 1 : line + len;
    }
    return text;
}
