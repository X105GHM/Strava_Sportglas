#pragma once
#include <span>
#include "esp_err.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "models/Types.hpp"

class LedStrip
{
    rmt_channel_handle_t channel_{};
    rmt_encoder_handle_t encoder_{};
    rmt_symbol_word_t *symbols_{};
    bool enabled_{};

public:
    LedStrip() = default;
    ~LedStrip();
    LedStrip(const LedStrip &) = delete;
    LedStrip &operator=(const LedStrip &) = delete;
    esp_err_t begin();
    esp_err_t show(std::span<const RgbColor> pixels);
    esp_err_t black();
    void release();
};
