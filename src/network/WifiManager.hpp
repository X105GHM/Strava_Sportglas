#pragma once
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "storage/PersistentState.hpp"
#include <cstdint>

class WifiManager
{
    EventGroupHandle_t events_{};
    esp_netif_t *netif_{};
    esp_event_handler_instance_t ipHandler_{}, wifiHandler_{};
    bool initialized_{}, started_{};
    static void event(void *, esp_event_base_t, int32_t, void *);
    bool waitForEndpoint(const char *url);

public:
    WifiManager() = default;
    ~WifiManager();
    WifiManager(const WifiManager &) = delete;
    WifiManager &operator=(const WifiManager &) = delete;
    bool connect(const DeviceCredentials &credentials);
    bool syncClock();
};
