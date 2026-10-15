#pragma once
#include <algorithm>
#include <array>
#include "config/Config.hpp"

class DisplayRenderer
{
    static RgbColor scale(RgbColor color, float gain)
    {
        gain = std::clamp(gain, 0.f, 1.f);
        return {static_cast<uint8_t>(color.r * gain), static_cast<uint8_t>(color.g * gain), static_cast<uint8_t>(color.b * gain)};
    }

    static RgbColor redGreen(float fraction)
    {
        const float p = std::clamp(fraction, 0.f, 1.f);
        if (p <= 0.5f) return {255, static_cast<uint8_t>(510.f * p), 0};
        return {static_cast<uint8_t>(510.f * (1.f - p)), 255, 0};
    }

public:
    using Frame = std::array<RgbColor, config::kLedCount>;

    static bool hasSportColor(const Frame &frame)
    {
        auto lit = [&](LedSegment segment)
        {
            for (std::size_t i = segment.first; i < segment.first + segment.count; ++i)
                if (frame[i].r || frame[i].g || frame[i].b) return true;
            return false;
        };

        return lit(config::kLayout.ride) || (config::kTriathlon && (lit(config::kLayout.swim) || lit(config::kLayout.run)));
    }

    static Frame blend(const Frame &from, const Frame &to, float fraction)
    {
        const float t = std::clamp(fraction, 0.f, 1.f);
        auto channel = [t](uint8_t a, uint8_t b)
        {
            return static_cast<uint8_t>(a + (int(b) - int(a)) * t + 0.5f);
        };

        Frame frame{};
        for (std::size_t i = 0; i < frame.size(); ++i)
            frame[i] = {channel(from[i].r, to[i].r), channel(from[i].g, to[i].g), channel(from[i].b, to[i].b)};

        return frame;
    }

    Frame render(DisplayMode mode, const WeeklySummary &data, float animation = 1.f, float pulse = 1.f) const
    {
        Frame frame{};
        if (mode == DisplayMode::Off) return frame;

        auto uniform = [&](LedSegment segment, RgbColor color, float gain = 1.f)
        {
            const auto pixel = scale(color, gain);
            for (std::size_t i = 0; i < segment.count; ++i)
                frame[segment.first + i] = pixel;
        };

        const auto &l = config::kLayout;
        uniform(l.background, colors::WarmWhite);

        if (mode == DisplayMode::WarmWhite)
        {
            for (auto segment : {l.swim, l.run, l.ride})
                uniform(segment, colors::WarmWhite);
            return frame;
        }

        auto sport = [&](LedSegment segment, [[maybe_unused]] RgbColor fixedColor, SportStats stats)
        {
            if (!stats.averageWeekMeters && !stats.currentMeters)
            {
                uniform(segment, {0, 0, 0});
                return;
            }

            const float p = std::clamp(progress(stats), 0.f, 1.f);
            const float overPulse = stats.averageWeekMeters && stats.currentMeters > stats.averageWeekMeters ? pulse : 1.f;

            #if defined(CONFIG_DISPLAY_SPORT_STYLE_RED_GREEN_SCALE)
            const float animatedProgress = p * std::clamp(animation, 0.f, 1.f);
            uniform(segment, redGreen(animatedProgress), overPulse);
            #else
            uniform(segment, fixedColor, p * animation * overPulse);
            #endif
        };

        if constexpr (config::kTriathlon)
        {
            sport(l.swim, colors::Swim, data.swim);
            sport(l.run, colors::Run, data.run);
        }

        // Im reinen Radmodus bleiben die übrigen Zonen warmweiß.
        sport(l.ride, colors::Ride, data.ride);
        return frame;
    }
};
