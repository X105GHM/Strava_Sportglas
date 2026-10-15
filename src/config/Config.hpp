#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include "sdkconfig.h"
#include "models/Types.hpp"

struct LedSegment
{
    std::size_t first, count;
};

struct DisplayLayout
{
    LedSegment background, swim, run, ride;
};

namespace config
{
    // Muss zum Profil in profiles.json und zum Gerätetoken passen.
    inline constexpr char kProfileName[] = "Danilo";
    static_assert(
        []
        {
            constexpr auto length = sizeof(kProfileName) - 1;
            if (length < 1 || length > 48) return false;
            for (std::size_t i = 0; i < length; ++i)
            {
                const char c = kProfileName[i];
                const bool alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                if (!alnum && (i == 0 || (c != '_' && c != '-'))) return false;
            }
            return true;
        }(),
        "Profile name: 1..48 ASCII letters/digits, underscore or dash; start with a letter/digit");
    #ifdef CONFIG_DISPLAY_CYCLING_ONLY
    inline constexpr bool kTriathlon = false;
    #else
    inline constexpr bool kTriathlon = true;
    #endif
    inline constexpr std::size_t kLedCount = 43;

    inline constexpr DisplayLayout kLayout{
        .background = {0, kLedCount},
        .swim = {7, 3}, // LEDs 8..10
        .run = {13, 3}, // LEDs 14..16
        .ride = {19, 3} // LEDs 20..22
    };

    inline constexpr uint8_t kBrightness = 255;
    inline constexpr uint32_t kCurrentLimitMa = 900;
    inline constexpr uint32_t kChannelFullScaleMa = 20;
    inline constexpr uint32_t kLedIdleMa = 1;

    inline constexpr uint64_t kWakeSeconds = 3600;
    inline constexpr uint64_t kFetchRetrySeconds = 300;
    inline constexpr uint64_t kFetchHardTimeoutSeconds = 90;
    inline constexpr uint32_t kInteractionIdleMs = 2000;
    inline constexpr uint64_t kTouchHealthSeconds = 30;
    inline constexpr int kLedTransmitTimeoutMs = 100;
    inline constexpr uint32_t kMaxMeters = 10000000;

    inline constexpr uint32_t kTouchThresholdPercent = 15;
    inline constexpr uint32_t kTouchWakeThresholdPercent = 15;
    inline constexpr uint32_t kTouchPressDebounceMs = 40;
    inline constexpr uint32_t kTouchReleaseDebounceMs = 40;
    inline constexpr uint32_t kTouchSampleMs = 20;
    inline constexpr std::size_t kTouchReleaseSamples = 5;
    inline constexpr std::size_t kTouchCalibrationSamples = 32;
    inline constexpr uint32_t kTouchReleaseTimeoutMs = 10000;
    inline constexpr uint32_t kTouchCalibrationTimeoutMs = 2000;
    inline constexpr uint32_t kFalseTouchCooldownSeconds = 1;

    #ifdef CONFIG_DISPLAY_PERSIST_TOUCH_CALIBRATION
    inline constexpr bool kPersistTouchCalibration = true;
    #else
    inline constexpr bool kPersistTouchCalibration = false;
    #endif
    inline constexpr uint32_t kTouchCalibrationRevision = 3;

    #ifdef CONFIG_IDF_TARGET_ESP32
    inline constexpr bool kTouchDecreases = true;
    #else
    inline constexpr bool kTouchDecreases = false;
    #endif

    inline constexpr int kData = CONFIG_DISPLAY_DATA_GPIO;
    inline constexpr int kPower = CONFIG_DISPLAY_POWER_GPIO;
    inline constexpr int kOe = CONFIG_DISPLAY_OE_GPIO;
    inline constexpr int kTouch = CONFIG_DISPLAY_TOUCH_GPIO;

    constexpr bool segmentValid(LedSegment s)
    {
        return s.first <= kLedCount && s.count <= kLedCount - s.first;
    }

    static_assert(kLedCount > 0 && kLedCount <= 512);
    static_assert(segmentValid(kLayout.background) && segmentValid(kLayout.swim) && segmentValid(kLayout.run) && segmentValid(kLayout.ride));

    inline constexpr auto kGamma = []
    {
        std::array<uint8_t, 256> a{};
        for (unsigned i = 0; i < 256; ++i)
            a[i] = static_cast<uint8_t>((i * i + 127) / 255);
        return a;
    }();
}

namespace colors
{
    inline constexpr RgbColor WarmWhite{255, 180, 105};
    inline constexpr RgbColor Swim{0, 180, 255};
    inline constexpr RgbColor Run{255, 65, 8};
    inline constexpr RgbColor Ride{20, 255, 35};
}
