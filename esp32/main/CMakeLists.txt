# Fault code descriptions are optional and user-supplied (not in git). If the CSV
# exists it is embedded in the firmware; otherwise faults show as codes only.
set(FAULT_CODES_CSV "${CMAKE_CURRENT_LIST_DIR}/../../ross-tech-fault-codes.csv")
set(text_files "${CMAKE_CURRENT_LIST_DIR}/../../assets/CREDITS.md" "${CMAKE_CURRENT_LIST_DIR}/../../assets/labels.csv")
if(EXISTS "${FAULT_CODES_CSV}")
    list(APPEND text_files "${FAULT_CODES_CSV}")
endif()

# Hero images: raw RGB565 from assets/tools/make_assets.py.
set(hero_dir "${CMAKE_CURRENT_LIST_DIR}/../../assets/out/p4")

idf_component_register(
    SRCS
        main.c
        assets.c
        board_display.c
        debug_console.c
        gui.c
        io_esp32_ftdi.c
        kline_task.c
        ../../kwcore/car_profile.c
        ../../kwcore/fake_ecu.c
        ../../kwcore/fault_table.c
        ../../kwcore/kw1281.c
        ../../kwcore/labels.c
    INCLUDE_DIRS . ../../kwcore
    REQUIRES usb esp_timer esp_lcd esp_driver_gpio esp_driver_i2c esp_driver_uart nvs_flash jc8012p4a1c
    EMBED_FILES
        "${hero_dir}/hero_eurovan.rgb565"
        "${hero_dir}/hero_harlequin.rgb565"
        "${hero_dir}/hero_family.rgb565"
    EMBED_TXTFILES ${text_files}
)

if(EXISTS "${FAULT_CODES_CSV}")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_FAULT_TABLE=1)
endif()
