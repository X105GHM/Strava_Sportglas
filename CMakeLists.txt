cmake_minimum_required(VERSION 3.16.0)

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
include("${CMAKE_CURRENT_LIST_DIR}/SyncDisplayConfig.cmake")
strava_sync_display_config("${SDKCONFIG_DEFAULTS}")

set(COMPONENTS src __pio_env)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Sportglas)
