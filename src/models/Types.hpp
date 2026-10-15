#pragma once
#include <array>
#include <cstdint>

struct RgbColor
{
    uint8_t r, g, b;
    bool operator==(const RgbColor &) const = default;
};
enum class DisplayMode : uint8_t
{
    Off,
    Sport,
    WarmWhite
};
enum class AppState
{
    Boot,
    EvaluateWakeup,
    FetchData,
    Render,
    PrepareSleep,
    Sleep
};

struct SportStats
{
    uint32_t currentMeters{}, averageWeekMeters{};
};

struct WeeklySummary
{
    SportStats swim{}, run{}, ride{};
};

struct SavedState
{
    uint32_t version{1};
    DisplayMode mode{DisplayMode::Off};
    bool hasSummary{};
    WeeklySummary summary{};
    int64_t generatedAt{};
};

inline float progress(SportStats s)
{
    return s.averageWeekMeters ? float(s.currentMeters) / s.averageWeekMeters : (s.currentMeters ? 1.f : 0.f);
}
