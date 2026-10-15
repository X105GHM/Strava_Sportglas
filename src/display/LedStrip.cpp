#include "LedStrip.hpp"
#include "config/Config.hpp"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include <array>

namespace
{
    constexpr std::size_t kSymbolCount = config::kLedCount * 24 + 1;
}

LedStrip::~LedStrip()
{
    release();
}

esp_err_t LedStrip::begin()
{
    if (channel_) return ESP_OK;

    // Symbole im internen RAM vorbereiten, damit die ISR während der Ausgabe nur kopieren muss.
    symbols_ = static_cast<rmt_symbol_word_t *>(heap_caps_calloc(kSymbolCount, sizeof(rmt_symbol_word_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!symbols_) return ESP_ERR_NO_MEM;

    rmt_tx_channel_config_t rmt{};
    rmt.gpio_num = static_cast<gpio_num_t>(config::kData);
    rmt.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt.resolution_hz = 10000000;
    rmt.mem_block_symbols = 64;
    rmt.trans_queue_depth = 1;
    rmt.intr_priority = 3;

    auto err = rmt_new_tx_channel(&rmt, &channel_);
    rmt_copy_encoder_config_t copy{};
    if (err == ESP_OK) err = rmt_new_copy_encoder(&copy, &encoder_);

    if (err == ESP_OK)
    {
        err = rmt_enable(channel_);
        enabled_ = err == ESP_OK;
    }

    if (err != ESP_OK) release();
    return err;
}

void LedStrip::release()
{
    if (enabled_) rmt_disable(channel_);
    enabled_ = false;
    if (channel_) rmt_del_channel(channel_);
    channel_ = nullptr;
    if (encoder_) rmt_del_encoder(encoder_);
    encoder_ = nullptr;

    heap_caps_free(symbols_);
    symbols_ = nullptr;
}

esp_err_t LedStrip::black()
{
    const std::array<RgbColor, config::kLedCount> pixels{};
    return show(pixels);
}

esp_err_t LedStrip::show(std::span<const RgbColor> pixels)
{
    if (!enabled_ || pixels.size() != config::kLedCount) return ESP_ERR_INVALID_ARG;

    std::array<RgbColor, config::kLedCount> output{};
    uint32_t channelSum = 0;
    auto transform = [](uint8_t v)
    {
        return config::kGamma[(unsigned(v) * config::kBrightness + 127) / 255];
    };

    for (std::size_t i = 0; i < pixels.size(); ++i)
    {
        output[i] = {transform(pixels[i].r), transform(pixels[i].g), transform(pixels[i].b)};
        channelSum += output[i].r + output[i].g + output[i].b;
    }

    const uint32_t idle = config::kLedCount * config::kLedIdleMa;
    const uint32_t available = config::kCurrentLimitMa > idle ? config::kCurrentLimitMa - idle : 0;
    const uint32_t requested = channelSum * config::kChannelFullScaleMa;
    const uint32_t budget = available * 255;

    std::size_t symbol = 0;
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        auto p = output[i];
        if (config::kCurrentLimitMa && requested > budget)
        {
            p.r = uint64_t(p.r) * budget / requested;
            p.g = uint64_t(p.g) * budget / requested;
            p.b = uint64_t(p.b) * budget / requested;
        }

        for (uint8_t channel : {p.g, p.r, p.b})
        {
            for (unsigned mask = 0x80; mask; mask >>= 1)
            {
                rmt_symbol_word_t bit{};
                bit.level0 = 1;
                bit.duration0 = channel & mask ? 9 : 3;
                bit.level1 = 0;
                bit.duration1 = channel & mask ? 3 : 9;
                symbols_[symbol++] = bit;
            }
        }
    }

    // 280 µs LOW bei 10 MHz; diese Latch-Zeit reicht auch für neuere WS2812B.
    rmt_symbol_word_t reset{};
    reset.duration0 = reset.duration1 = 1400;
    symbols_[symbol] = reset;

    rmt_transmit_config_t tx{};
    tx.flags.queue_nonblocking = true;
    auto err = rmt_transmit(channel_, encoder_, symbols_, kSymbolCount * sizeof(*symbols_), &tx);
    if (err == ESP_OK) err = rmt_tx_wait_all_done(channel_, config::kLedTransmitTimeoutMs);

    // Bei fehlender Fertigmeldung RMT abbrechen, bevor der Sendepuffer freigegeben wird.
    if (err != ESP_OK) release();
    return err;
}
