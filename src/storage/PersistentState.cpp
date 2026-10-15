#include "PersistentState.hpp"
#include "config/Config.hpp"
#include "models/TouchCalibration.hpp"
#include <cstring>
#include <algorithm>
#if __has_include("config/secrets.local.hpp")
#include "config/secrets.local.hpp"
#define DISPLAY_HAS_LOCAL_SECRETS 1
#endif

PersistentState::PersistentState()
{
    ESP_ERROR_CHECK(nvs_open("display", NVS_READWRITE, &state_));
    if (nvs_open("device", NVS_READONLY, &device_) != ESP_OK) device_ = 0;
}

PersistentState::~PersistentState()
{
    if (state_) nvs_close(state_);
    if (device_) nvs_close(device_);
}

SavedState PersistentState::load()
{
    SavedState value{};
    std::size_t size = sizeof(value);
    if (nvs_get_blob(state_, "state", &value, &size) != ESP_OK || size != sizeof(value) || value.version != 1 || static_cast<unsigned>(value.mode) > 2) return {};

    for (auto s : {value.summary.swim, value.summary.run, value.summary.ride})
        if (s.currentMeters > config::kMaxMeters || s.averageWeekMeters > config::kMaxMeters) return {};

    return value;
}

esp_err_t PersistentState::save(const SavedState &value)
{
    auto err = nvs_set_blob(state_, "state", &value, sizeof(value));
    return err == ESP_OK ? nvs_commit(state_) : err;
}

uint32_t PersistentState::loadTouchBaseline()
{
    TouchCalibration value{};
    std::size_t size = sizeof(value);
    if (nvs_get_blob(state_, "touch_cal", &value, &size) != ESP_OK || size != sizeof(value) || !value.validFor(config::kTouchCalibrationRevision, config::kTouch)) return 0;
    return value.baseline;
}

esp_err_t PersistentState::saveTouchBaseline(uint32_t baseline)
{
    const TouchCalibration value{1, config::kTouchCalibrationRevision, uint32_t(config::kTouch), baseline};
    if (!value.validFor(config::kTouchCalibrationRevision, config::kTouch)) return ESP_ERR_INVALID_ARG;

    const auto err = nvs_set_blob(state_, "touch_cal", &value, sizeof(value));
    return err == ESP_OK ? nvs_commit(state_) : err;
}

bool PersistentState::credentials(DeviceCredentials &result)
{
    result = {};
    #ifdef DISPLAY_HAS_LOCAL_SECRETS
    if constexpr (local_secrets::kEnabled)
    {
        auto copy = []<std::size_t N, std::size_t M>(std::array<char, N> &dest, const char (&value)[M])
        {
            static_assert(M <= N, "Local credential exceeds its maximum length");
            std::copy_n(value, M, dest.begin());
        };

        copy(result.ssid, local_secrets::kSsid);
        copy(result.password, local_secrets::kPassword);
        copy(result.endpoint, local_secrets::kEndpoint);
        copy(result.token, local_secrets::kDeviceToken);
    }
    else
    #endif
    {
        if (!device_) return false;

        auto get = [&](const char *key, auto &dest)
        {
            std::size_t n = dest.size();
            return nvs_get_str(device_, key, dest.data(), &n) == ESP_OK;
        };
        if (!get("ssid", result.ssid) || !get("password", result.password) || !get("endpoint", result.endpoint) || !get("token", result.token)) return false;
    }

    const auto passwordLength = std::strlen(result.password.data());
    if (!result.ssid[0] || passwordLength < 8 || passwordLength > 63 || std::strncmp(result.endpoint.data(), "https://", 8) != 0 || std::strlen(result.token.data()) != 64) return false;
    for (std::size_t i = 0; i < 64; ++i)
        if (!((result.token[i] >= '0' && result.token[i] <= '9') || (result.token[i] >= 'a' && result.token[i] <= 'f'))) return false;

    // Zugangsdaten und Fragmente in der URL sowie eingeschleuste Header-Zeilen ausschließen.
    for (char c : result.endpoint)
        if (c == '\r' || c == '\n' || c == '@' || c == '#') return false;
    return true;
}
