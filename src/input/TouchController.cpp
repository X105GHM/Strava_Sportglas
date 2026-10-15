#include "TouchController.hpp"
#include "config/Config.hpp"
#include "driver/touch_sensor.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_idf_version.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
#include "hal/touch_sens_hal.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <array>

namespace
{
    RTC_DATA_ATTR uint32_t retainedBaseline = 0;
    RTC_DATA_ATTR uint32_t retainedCalibrationRevision = 0;

    auto channel()
    {
        return static_cast<touch_pad_t>(config::kTouch);
    }

    uint32_t nowMs()
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
}

TouchController::~TouchController()
{
    if (started_) touch_pad_deinit();
}

esp_err_t TouchController::begin(uint32_t storedBaseline, bool awaitingRelease)
{
    auto err = touch_pad_init();
    if (err != ESP_OK) return err;
    started_ = true;
    if ((err = touch_pad_config(channel())) != ESP_OK) return err;
    touch_filter_config_t filter{};
    filter.mode = TOUCH_PAD_FILTER_IIR_16;
    filter.debounce_cnt = 5;
    filter.noise_thr = 2;
    filter.jitter_step = 4;
    filter.smh_lvl = TOUCH_PAD_SMOOTH_IIR_2;
    if ((err = touch_pad_filter_set_config(&filter)) != ESP_OK) return err;
    if ((err = touch_pad_filter_enable()) != ESP_OK) return err;
    // Ungenutzte Kanäle für eine ruhigere Messung der Glaselektrode auf GND legen.
    if ((err = touch_pad_set_idle_channel_connect(TOUCH_PAD_CONN_GND)) != ESP_OK) return err;
    // Über denselben Messpfad kalibrieren, der im Schlaf aktiv bleibt.
    if ((err = touch_pad_sleep_channel_enable(channel(), true)) != ESP_OK) return err;
    if ((err = touch_pad_sleep_channel_enable_proximity(channel(), false)) != ESP_OK) return err;
    if ((err = touch_pad_sleep_channel_set_work_time(1000, TOUCH_PAD_MEASURE_CYCLE_DEFAULT)) != ESP_OK) return err;
    if ((err = touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER)) != ESP_OK) return err;
    if ((err = touch_pad_fsm_start()) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));
    baseline_ = retainedCalibrationRevision == config::kTouchCalibrationRevision && retainedBaseline ? retainedBaseline : storedBaseline;
    // Ein bereits gezählter Wakeup darf bei gehaltenem Finger nicht erneut zählen.
    debouncer_.reset(awaitingRelease || !baseline_);
    return ESP_OK;
}

bool TouchController::pollPress()
{
    uint32_t raw = 0;
    const auto err = started_ ? touch_pad_sleep_channel_read_data(channel(), &raw) : ESP_ERR_INVALID_STATE;
    if (err != ESP_OK || !baseline_) return false;
    const bool pressed = debouncer_.update(raw, baseline_, nowMs());
    return pressed;
}

void TouchController::resume(bool countedPress)
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
    touch_pad_clear_status();
    if (countedPress) debouncer_.reset(true);
}

esp_err_t TouchController::recover()
{
    resume(false);
    if (started_) touch_pad_deinit();
    started_ = false;
    return begin(baseline_, true);
}

SleepPreparation TouchController::prepareSleep()
{
    // Eine alte Wakeup-Konfiguration auch bei erneutem Versuch zurücknehmen.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
    std::array<uint32_t, config::kTouchCalibrationSamples> samples{};
    const auto targetSamples = baseline_ ? config::kTouchReleaseSamples : samples.size();
    static_assert(config::kTouchReleaseSamples <= config::kTouchCalibrationSamples);
    unsigned stable = 0;
    // Bei ausbleibender Freigabe zunächst nur per Timer wieder aufwecken.
    for (unsigned i = 0; i < config::kTouchReleaseTimeoutMs / config::kTouchSampleMs && stable < targetSamples; ++i)
    {
        uint32_t raw = 0;
        const auto readErr = touch_pad_sleep_channel_read_data(channel(), &raw);
        if (readErr != ESP_OK || raw == 0) return SleepPreparation::Retry;
        if (debouncer_.update(raw, baseline_, nowMs())) return SleepPreparation::NewPress;
        if (baseline_ && raw > baseline_ + baseline_ * config::kTouchThresholdPercent / 200)
            stable = 0;
        else
            samples[stable++] = raw;
        vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
    }
    if (stable < targetSamples)
    {
        ESP_LOGW("touch", "No stable release; baseline=%lu", (unsigned long)baseline_);
        return SleepPreparation::Retry;
    }
    std::sort(samples.begin(), samples.begin() + targetSamples);
    uint32_t candidate = samples[targetSamples / 2];
    if (samples[targetSamples - 1] - samples.front() > candidate / 20) return SleepPreparation::Retry;
    baseline_ = candidate;
    retainedBaseline = baseline_;
    retainedCalibrationRevision = config::kTouchCalibrationRevision;

    uint32_t smooth = 0;
    uint32_t benchmark = 0;
    if (touch_pad_sleep_channel_read_smooth(channel(), &smooth) != ESP_OK) return SleepPreparation::Retry;
    if (touch_pad_sleep_channel_read_benchmark(channel(), &benchmark) != ESP_OK) return SleepPreparation::Retry;
    if (!smooth || !benchmark) return SleepPreparation::Retry;

    const auto delta = std::max(uint32_t(1), smooth * config::kTouchWakeThresholdPercent / 100);
    if (touch_pad_sleep_set_threshold(channel(), delta) != ESP_OK) return SleepPreparation::Retry;

    touch_pad_clear_status();
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)

    touch_hal_save_sleep_config(config::kTouch, nullptr, true);
    #endif

    if (esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON) != ESP_OK) return SleepPreparation::Retry;
    if (esp_sleep_enable_touchpad_wakeup() != ESP_OK) return SleepPreparation::Retry;

    return SleepPreparation::Ready;
}
