#include "PowerManager.hpp"
#include "config/Config.hpp"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    auto pin(int n)
    {
        return static_cast<gpio_num_t>(n);
    }

    [[maybe_unused]] bool allowed(int n)
    {
        #ifdef CONFIG_IDF_TARGET_ESP32
        // PICO-MINI-02: Boot-Straps, UART0 und interne Speicherpins aussparen.
        return n == 4 || (n >= 13 && n <= 14) || (n >= 18 && n <= 19) || (n >= 21 && n <= 23) || (n >= 25 && n <= 27) || n == 32 || n == 33;
        #else
        // S3: Boot-Straps, USB, Speicher, Debug-Pins und die Onboard-LED an GPIO48 aussparen.
        // Die tatsächliche Platinenbelegung muss zusätzlich geprüft sein.
        return (n >= 1 && n <= 18 && n != 3) || n == 21 || n == 38 || n == 47;
        #endif
    }

    void set(int p, int level)
    {
        ESP_ERROR_CHECK(gpio_set_level(pin(p), level));
    }

    void output(int p, int level)
    {
        set(p, level);

        // Auch RTC-fähige Pins ausdrücklich auf den digitalen GPIO-Mux schalten.
        gpio_config_t cfg{};
        cfg.pin_bit_mask = 1ULL << p;
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        cfg.intr_type = GPIO_INTR_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&cfg));
        ESP_ERROR_CHECK(gpio_hold_dis(pin(p)));
    }
}

bool PowerManager::validPins()
{
    #ifndef CONFIG_DISPLAY_BOARD_VERIFIED
    return false;
    #else
    const int d = config::kData, p = config::kPower, o = config::kOe, t = config::kTouch;
    const bool bufferValid = o == -1 || (allowed(o) && o != d && o != p && o != t);
    #ifdef CONFIG_IDF_TARGET_ESP32
    const bool touchValid = t == 4 || t == 13 || t == 14 || t == 27 || t == 32 || t == 33;
    #else
    const bool touchValid = t >= 1 && t <= 14 && t != 3;
    #endif
    return allowed(d) && allowed(p) && bufferValid && touchValid && d != p && d != t && p != t;
    #endif
}

void PowerManager::begin(bool retainedOn)
{
    // Auf der Platine liegt OE# des AHCT-Buffers fest auf GND.
    if constexpr (config::kOe >= 0) output(config::kOe, 0);
    output(config::kData, 0);
    output(config::kPower, retainedOn ? 1 : 0);
    gpio_deep_sleep_hold_dis();
}

void PowerManager::enable()
{
    set(config::kPower, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void PowerManager::outputEnable(bool enabled)
{
    if constexpr (config::kOe >= 0) set(config::kOe, enabled ? 1 : 0);
}

void PowerManager::parkData()
{
    // Erst nach der RMT-Freigabe auf GPIO-Ausgabe LOW umschalten.
    output(config::kData, 0);
}

void PowerManager::off()
{
    outputEnable(false);
    parkData();
    set(config::kPower, 0);
}

esp_err_t PowerManager::sleep(bool powered, uint64_t seconds)
{
    outputEnable(false);
    parkData();
    set(config::kPower, powered ? 1 : 0);

    for (int p : {config::kData, config::kPower})
        ESP_ERROR_CHECK(gpio_hold_en(pin(p)));
    if constexpr (config::kOe >= 0) ESP_ERROR_CHECK(gpio_hold_en(pin(config::kOe)));

    auto err = esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    if (err == ESP_OK) err = esp_light_sleep_start();

    // Auch bei abgewiesenem Sleep entriegeln, sonst könnte GPIO19 beim Einschalten auf LOW festhängen.
    for (int p : {config::kData, config::kPower})
        ESP_ERROR_CHECK(gpio_hold_dis(pin(p)));
    if constexpr (config::kOe >= 0) ESP_ERROR_CHECK(gpio_hold_dis(pin(config::kOe)));

    return err;
}
