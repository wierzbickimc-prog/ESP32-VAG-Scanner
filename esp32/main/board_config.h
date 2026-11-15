#pragma once

#include "sdkconfig.h"

#if CONFIG_BOARD_JC4880P443
#define BOARD_NAME "Guition JC4880P443"
#define BOARD_WIDTH 480
#define BOARD_HEIGHT 800
#else
#define BOARD_NAME "Guition JC8012P4A1C"
#define BOARD_WIDTH 800
#define BOARD_HEIGHT 1280
#endif
