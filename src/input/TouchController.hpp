#pragma once
#include <cstdint>
#include <array>
#include "esp_err.h"
#include "TouchDebouncer.hpp"

enum class SleepPreparation
{
    Ready,
    NewPress,
    Retry,
    Pending
};

class TouchController
{
    uint32_t baseline_{};
    bool started_{};
    TouchDebouncer debouncer_;
    std::array<uint32_t, config::kTouchCalibrationSamples> sleepSamples_{};
    unsigned sleepSampleCount_{};
    uint32_t sleepStartedMs_{}, lastSampleMs_{}, lastReadyMs_{};
    bool preparing_{}, wakeArmed_{};

public:
    TouchController() = default;
    ~TouchController();
    TouchController(const TouchController &) = delete;
    TouchController &operator=(const TouchController &) = delete;
    esp_err_t begin(uint32_t storedBaseline, bool awaitingRelease);

    uint32_t baseline() const
    {
        return baseline_;
    }

    bool pollPress();
    // Den bereits gezählten Wakeup bestätigen, damit ein gehaltener Finger nicht doppelt zählt.
    void resume(bool countedPress);
    esp_err_t recover();
    SleepPreparation prepareSleep();
};
