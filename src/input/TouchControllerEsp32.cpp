#include "TouchController.hpp"
#include "config/Config.hpp"
#include "driver/touch_sensor.h"
#include "hal/touch_sensor_hal.h"
#include "esp_sleep.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <array>
#ifdef STRAVA_TOUCH_DIAGNOSTICS
#include <cstdio>
#endif

namespace
{
    RTC_DATA_ATTR uint32_t retainedBaseline = 0;
    RTC_DATA_ATTR uint32_t retainedCalibrationRevision = 0;

    // Beim klassischen ESP32 sind GPIO- und Touch-Kanalnummer verschieden.
    constexpr touch_pad_t channel()
    {
        switch (config::kTouch)
        {
        case 4:
            return TOUCH_PAD_NUM0;
        case 13:
            return TOUCH_PAD_NUM4;
        case 14:
            return TOUCH_PAD_NUM6;
        case 27:
            return TOUCH_PAD_NUM7;
        case 33:
            return TOUCH_PAD_NUM8;
        case 32:
            return TOUCH_PAD_NUM9;
        default:
            return TOUCH_PAD_MAX;
        }
    }

    static_assert(channel() != TOUCH_PAD_MAX, "Unsupported ESP32 touch GPIO");

    uint32_t nowMs()
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }

    void diagnosticSample(uint32_t raw, uint32_t baseline, const char *phase)
    {
        #ifdef STRAVA_TOUCH_DIAGNOSTICS
        static uint32_t reported = 0;
        if (uint32_t(nowMs() - reported) >= 200)
        {
            reported = nowMs();
            std::printf("TOUCH sample raw=%lu baseline=%lu phase=%s\n", static_cast<unsigned long>(raw), static_cast<unsigned long>(baseline), phase);
        }
        #else
        (void)raw;
        (void)baseline;
        (void)phase;
        #endif
    }

    esp_err_t read(uint32_t &value)
    {
        // touch_pad_read() wartet unbegrenzt auf die FSM. Nur fertige Messwerte direkt auslesen.
        if (!touch_pad_meas_is_done()) return ESP_ERR_NOT_FINISHED;
        value = touch_hal_read_raw_data(channel());
        return value ? ESP_OK : ESP_ERR_INVALID_STATE;
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

    if ((err = touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER)) != ESP_OK) return err;
    // Kürzere Messpausen erfassen auch kurze Berührungen.
    if ((err = touch_pad_set_measurement_interval(0x400)) != ESP_OK) return err;

    // Schwelle 0 sperrt die Erkennung bis zur Kalibrierung.
    if ((err = touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW)) != ESP_OK) return err;
    if ((err = touch_pad_set_trigger_source(TOUCH_TRIGGER_SOURCE_SET1)) != ESP_OK) return err;
    if ((err = touch_pad_config(channel(), 0)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(150));

    baseline_ = retainedCalibrationRevision == config::kTouchCalibrationRevision && retainedBaseline ? retainedBaseline : storedBaseline;
    if (baseline_ > UINT16_MAX) baseline_ = 0;
    debouncer_.reset(awaitingRelease || !baseline_);
    return ESP_OK;
}

bool TouchController::pollPress()
{
    uint32_t raw = 0;
    if (!started_ || !baseline_ || read(raw) != ESP_OK) return false;
    diagnosticSample(raw, baseline_, debouncer_.phaseName());
    return debouncer_.update(raw, baseline_, nowMs());
}

void TouchController::resume(bool countedPress)
{
    if (wakeArmed_) esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
    wakeArmed_ = false;
    preparing_ = false;
    sleepSampleCount_ = 0;
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
    const auto now = nowMs();
    if (wakeArmed_)
    {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
        wakeArmed_ = false;
    }

    if (!preparing_)
    {
        preparing_ = true;
        sleepSampleCount_ = 0;
        sleepStartedMs_ = lastReadyMs_ = now;
        lastSampleMs_ = now - config::kTouchSampleMs;
    }

    auto retry = [&]
    {
        preparing_ = false;
        sleepSampleCount_ = 0;
        return SleepPreparation::Retry;
    };

    if (!started_) return retry();
    if (!baseline_ && uint32_t(now - sleepStartedMs_) >= config::kTouchCalibrationTimeoutMs) return retry();
    if (uint32_t(now - lastSampleMs_) < config::kTouchSampleMs) return SleepPreparation::Pending;

    lastSampleMs_ = now;
    uint32_t raw = 0;
    const auto err = read(raw);
    if (err == ESP_ERR_NOT_FINISHED)
    {
        return uint32_t(now - lastReadyMs_) >= config::kTouchCalibrationTimeoutMs ? retry() : SleepPreparation::Pending;
    }
    if (err != ESP_OK) return retry();

    diagnosticSample(raw, baseline_, debouncer_.phaseName());
    lastReadyMs_ = now;
    if (debouncer_.update(raw, baseline_, now))
    {
        preparing_ = false;
        sleepSampleCount_ = 0;
        return SleepPreparation::NewPress;
    }

    // Bei gehaltenem Pad wach bleiben, damit die nächste Berührung nach dem Loslassen erkannt wird.
    if (baseline_ && !debouncer_.isReleased(raw, baseline_))
    {
        sleepSampleCount_ = 0;
        return SleepPreparation::Pending;
    }

    sleepSamples_[sleepSampleCount_++] = raw;
    const auto targetSamples = baseline_ ? config::kTouchReleaseSamples : sleepSamples_.size();
    if (sleepSampleCount_ < targetSamples) return SleepPreparation::Pending;

    std::sort(sleepSamples_.begin(), sleepSamples_.begin() + targetSamples);
    const uint32_t candidate = sleepSamples_[targetSamples / 2];
    if (sleepSamples_[targetSamples - 1] - sleepSamples_.front() > candidate / 20)
    {
        sleepSampleCount_ = 0;
        return SleepPreparation::Pending;
    }

    preparing_ = false;
    sleepSampleCount_ = 0;
    if (!candidate) return SleepPreparation::Retry;

    baseline_ = retainedBaseline = candidate;
    retainedCalibrationRevision = config::kTouchCalibrationRevision;

    const auto delta = std::max(uint32_t(1), baseline_ * config::kTouchWakeThresholdPercent / 100);
    if (delta >= baseline_) return SleepPreparation::Retry;
    if (touch_pad_set_thresh(channel(), static_cast<uint16_t>(baseline_ - delta)) != ESP_OK) return SleepPreparation::Retry;
    touch_pad_clear_status();

    // Touch-Wakeup braucht den RTC-Timer; RTC_PERIPH darf nicht fest auf ON stehen.
    if (esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO) != ESP_OK) return SleepPreparation::Retry;
    if (esp_sleep_enable_touchpad_wakeup() != ESP_OK) return SleepPreparation::Retry;
    wakeArmed_ = true;
    return SleepPreparation::Ready;
}
