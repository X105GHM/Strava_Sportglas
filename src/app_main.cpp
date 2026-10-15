#include "AppController.hpp"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include <cstdlib>
#include <ctime>
#include <initializer_list>

extern "C" void app_main()
{
    #ifdef CONFIG_LOG_DYNAMIC_LEVEL_CONTROL
    esp_log_level_set("*", ESP_LOG_WARN);
    for (const auto *tag : {"app", "fetch", "api", "network"})
        esp_log_level_set(tag, ESP_LOG_INFO);
    #endif
    setenv("TZ", "UTC0", 1);
    tzset();

    if (nvs_flash_init() != ESP_OK) return;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    AppController app;
    app.run();
}
