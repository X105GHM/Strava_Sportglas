set(debug_requires)
if(CONFIG_DISPLAY_SERIAL_DEBUG AND NOT CONFIG_IDF_TARGET_ESP32)
    list(APPEND debug_requires esp_driver_usb_serial_jtag)
endif()
set(touch_source "input/TouchController.cpp")
if(CONFIG_IDF_TARGET_ESP32)
    set(touch_source "input/TouchControllerEsp32.cpp")
endif()
idf_component_register(SRCS "app_main.cpp" "AppController.cpp" "display/LedStrip.cpp"
    "${touch_source}" "network/ApiClient.cpp" "network/WifiManager.cpp" "network/WeeklyParser.cpp"
    "storage/PersistentState.cpp" "power/PowerManager.cpp"
    INCLUDE_DIRS "." REQUIRES driver esp_wifi esp_event esp_netif esp_http_client
    mbedtls nvs_flash json esp_timer lwip http_parser led_strip hal ${debug_requires})
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
