#pragma once
/* Evaluate log arguments so startup uses the same calls as the firmware. */
#include <stdarg.h>
static inline void test_log(const char *tag, const char *format, ...)
{
    (void)tag;
    (void)format;
}
#define ESP_LOGI test_log
#define ESP_LOGW test_log
