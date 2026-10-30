if(NOT CMAKE_SCRIPT_MODE_FILE)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/wifi_setup.html" HG_WIFI_SETUP_HTML)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/wifi_setup.html")
    configure_file(wifi_setup_page.hpp.in wifi_setup_page.hpp @ONLY)
endif()

set(HG_P4_SOURCES)
set(HG_P4_REQUIRES)
if(IDF_TARGET STREQUAL "esp32p4")
    list(APPEND HG_P4_SOURCES
        "port_dsi.cpp"
        "port_audio_p4.cpp"
        "port_power_p4.cpp"
        "../../drivers/stc8_battery.cpp")
    list(APPEND HG_P4_REQUIRES esp_lcd_ek79007)
endif()
set(HG_WAKE_SOURCES)
set(HG_WAKE_REQUIRES)
# The wake word is a build option that promises a detector in the image. Until
# port_wake.cpp (the AFE + MultiNet engine) and the esp-sr component it needs are in the
# tree, turning the option on still builds - and ships a firmware whose wake word never
# fires, with no error anywhere to say why. Fail the build instead, so a promise with no
# engine behind it cannot reach a device. Clear the guard only by doing all three:
# port_wake.cpp landed, esp-sr required here, and CONFIG_HG_WAKE_MULTINET=y in the
# build's sdkconfig.
if(CONFIG_HG_WAKE_MULTINET)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/port_wake.cpp")
        message(FATAL_ERROR
            "HG_WAKE_MULTINET=y, but main/port_wake.cpp does not exist: nothing would run the "
            "detector and the device would silently never wake. Either turn the option off or "
            "add the engine (main/port_wake.cpp plus the espressif/esp-sr dependency in "
            "idf_component.yml).")
    endif()
    list(APPEND HG_WAKE_SOURCES "port_wake.cpp")
    list(APPEND HG_WAKE_REQUIRES esp-sr)
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "board.cpp"
        ${HG_WAKE_SOURCES}
        "port_amoled.cpp"
        "port_audio.cpp"
        ${HG_P4_SOURCES}
        "port_ble.cpp"
        "port_buttons.cpp"
        "port_codec.cpp"
        "port_cores3.cpp"
        "port_tca9554.cpp"
        "port_console.cpp"
        "port_diag.cpp"
        "port_display.cpp"
        "port_events.cpp"
        "port_ota.cpp"
        "port_parallel.cpp"
        "port_rgb.cpp"
        "port_serial.cpp"
        "port_power.cpp"
        "port_latch_power.cpp"
        "port_opus.cpp"
        "../../drivers/axp2101.cpp"
        "../../drivers/cores3.cpp"
        "port_storage.cpp"
        "port_system.cpp"
        "port_touch.cpp"
        "port_wifi.cpp"
        "port_wifi_setup.cpp"
        "port_ws.cpp"
    INCLUDE_DIRS "." "../../drivers" "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES core
    # "driver" pulls in GPIO, I2S, LEDC and SPI on every ESP-IDF 5.x release.
    # ESP-IDF 6.x dropped I2S and LEDC from that catch-all, so name them too;
    # both components exist in 5.x as well, where this is a no-op.
    PRIV_REQUIRES
        app_update
        bt
        driver
        esp_driver_i2s
        esp_driver_ledc
        esp_app_format
        esp_adc
        esp_event
        esp_lcd
        esp_lcd_st7701
        esp_lcd_panel_io_additions
        ${HG_P4_REQUIRES}
        ${HG_WAKE_REQUIRES}
        esp_netif
        esp_http_server
        esp_psram
        esp_timer
        esp_wifi
        console
        nvs_flash
        mbedtls
        esp_websocket_client
)

# Bring-up counters for the wake path (Kconfig: Wake word bring-up diagnostics). Off in a
# release build, so the relay log keeps only `hit:` and the lifecycle lines.
if(CONFIG_HG_WAKE_DIAG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE HG_WAKE_DIAG=1)
endif()
