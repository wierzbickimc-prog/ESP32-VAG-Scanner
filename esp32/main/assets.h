#pragma once

#include "lvgl.h"

#define HERO_W 800
#define HERO_H 640

/* Hero image for a car profile key ("eurovan", "harlequin", "family"); never NULL. */
const lv_image_dsc_t *assets_hero(const char *key);

/* Photo credits text (one "name: author, licence" per line). */
const char *assets_credits(void);
