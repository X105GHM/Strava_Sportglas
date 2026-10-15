#pragma once
#include "nvs.h"
#include "models/Types.hpp"
#include <array>

struct DeviceCredentials
{
    std::array<char, 33> ssid{};
    std::array<char, 65> password{};
    std::array<char, 257> endpoint{};
    std::array<char, 65> token{};
};

class PersistentState
{
    nvs_handle_t state_{}, device_{};

public:
    PersistentState();
    ~PersistentState();
    PersistentState(const PersistentState &) = delete;
    PersistentState &operator=(const PersistentState &) = delete;
    SavedState load();
    esp_err_t save(const SavedState &state);
    uint32_t loadTouchBaseline(); // 0 bedeutet: Erstkalibrierung nötig.
    esp_err_t saveTouchBaseline(uint32_t baseline);
    bool credentials(DeviceCredentials &result);
};
