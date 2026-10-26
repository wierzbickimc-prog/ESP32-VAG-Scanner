# JD9365 panel and GSL3680 touch drivers for the Guition JC8012P4A1C, taken from
# https://github.com/chipguyhere/JC8012P4A1C_display (MIT, see LICENSE) with the
# Arduino-only guards removed.
idf_component_register(
    SRCS esp_lcd_jd9365.c gsl3680_fw.c gsl_point_id.c
    INCLUDE_DIRS .
    REQUIRES esp_lcd esp_driver_gpio esp_driver_i2c
)

# Vendored as-is; don't let its warnings drown out ours.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-type-limits)
