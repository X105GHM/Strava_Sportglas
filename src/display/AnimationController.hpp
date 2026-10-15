#pragma once
#include "DisplayRenderer.hpp"
#include "LedStrip.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class AnimationController
{
public:
    struct Result
    {
        esp_err_t error;
        bool interrupted;
    };

    template <typename NextPress> Result transition(LedStrip &leds, const DisplayRenderer::Frame &from, const DisplayRenderer::Frame &to, NextPress nextPress)
    {
        if (from == to) return {ESP_OK, false};

        constexpr unsigned frames = 40;
        for (unsigned i = 0; i <= frames; ++i)
        {
            if (nextPress()) return {ESP_OK, true};
            const float t = float(i) / frames;
            // Flache Steigung an beiden Enden des Farbübergangs.
            const float ease = t * t * (3.f - 2.f * t);
            const auto err = leds.show(DisplayRenderer::blend(from, to, ease));
            if (err != ESP_OK) return {err, false};
            if (i < frames) vTaskDelay(pdMS_TO_TICKS(20));
        }
        return {ESP_OK, false};
    }

    template <typename NextPress> Result play(LedStrip &leds, const DisplayRenderer &renderer, const WeeklySummary &data, NextPress nextPress)
    {
        constexpr unsigned frames = 15;
        for (unsigned i = 0; i <= frames; ++i)
        {
            if (nextPress()) return {ESP_OK, true};
            float t = float(i) / frames;
            float ease = 1 - (1 - t) * (1 - t) * (1 - t);
            auto err = leds.show(renderer.render(DisplayMode::Sport, data, ease));
            if (err != ESP_OK) return {err, false};
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        auto exceeded = [](SportStats s)
        {
            return s.averageWeekMeters && s.currentMeters > s.averageWeekMeters;
        };
        bool over = exceeded(data.ride);
        if constexpr (config::kTriathlon) over |= exceeded(data.swim) || exceeded(data.run);

        if (over)
            for (unsigned i = 0; i <= 20; ++i)
            {
                if (nextPress()) return {ESP_OK, true};
                float t = float(i) / 20;
                auto err = leds.show(renderer.render(DisplayMode::Sport, data, 1, 1 - .18f * 4 * t * (1 - t)));
                if (err != ESP_OK) return {err, false};
                vTaskDelay(pdMS_TO_TICKS(25));
            }

        return {leds.show(renderer.render(DisplayMode::Sport, data)), false};
    }
};
