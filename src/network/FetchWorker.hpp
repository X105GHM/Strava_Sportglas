#pragma once
#include "ApiClient.hpp"
#include "WifiManager.hpp"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

// Ein Abruf zur Zeit. NVS, Touch und LEDs bleiben im Haupttask.
// Ein Ergebnis wird erst veröffentlicht, wenn alle Netzwerkressourcen freigegeben sind.
class FetchWorker
{
public:
    struct Result
    {
        SavedState state{};
        bool success{};
    };

private:
    DeviceCredentials credentials_{};
    SavedState snapshot_{};
    QueueHandle_t result_{};
    bool running_{}; // Nur der Haupttask greift darauf zu.

    static void run(void *arg)
    {
        auto &self = *static_cast<FetchWorker *>(arg);
        Result result{self.snapshot_, false};

        {
            WifiManager wifi;
            if (!wifi.connect(self.credentials_))
                ESP_LOGW("fetch", "Wi-Fi/network setup failed");
            else if (!wifi.syncClock())
                ESP_LOGW("fetch", "Clock synchronization failed");
            else
            {
                ESP_LOGI("fetch", "Clock ready; starting HTTPS");
                ApiClient api;
                result.success = api.fetch(self.credentials_, result.state);
            }
        } // WLAN und Treiber müssen vor der Ergebnismeldung beendet sein.

        xQueueSend(self.result_, &result, 0);
        // Ab hier darf der Haupttask den nächsten Abruf starten oder schlafen gehen.
        vTaskDelete(nullptr);
    }

public:
    FetchWorker() = default;
    FetchWorker(const FetchWorker &) = delete;
    FetchWorker &operator=(const FetchWorker &) = delete;

    // Die Queue bleibt über Light Sleep erhalten.
    bool start(const DeviceCredentials &credentials, const SavedState &snapshot)
    {
        if (running_) return false;

        credentials_ = credentials;
        snapshot_ = snapshot;

        if (!result_) result_ = xQueueCreate(1, sizeof(Result));
        if (!result_) return false;

        if (xTaskCreate(run, "display-fetch", 12288, this, 1, nullptr) != pdPASS)
        {
            vQueueDelete(result_);
            result_ = nullptr;
            return false;
        }

        running_ = true;
        return true;
    }

    bool poll(Result &result)
    {
        if (!running_ || xQueueReceive(result_, &result, 0) != pdTRUE) return false;

        running_ = false;
        return true;
    }
};
