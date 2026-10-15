#include "ApiClient.hpp"
#include "WeeklyParser.hpp"
#include "config/Config.hpp"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <ctime>
#include <cstdio>
#include <string>

namespace
{
    struct Http
    {
        esp_http_client_handle_t h;

        ~Http()
        {
            if (h)
            {
                esp_http_client_close(h);
                esp_http_client_cleanup(h);
            }
        }
    };

}

bool ApiClient::fetch(const DeviceCredentials &c, SavedState &state)
{
    // Die gesamte Antwort begrenzen; einzelne erfolgreiche Reads dürfen das Timeout nicht endlos verlängern.
    const auto deadline = esp_timer_get_time() + 20000000;
    esp_http_client_config_t cfg{};
    cfg.url = c.endpoint.data();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    cfg.disable_auto_redirect = true;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;

    Http client{esp_http_client_init(&cfg)};
    if (!client.h) return false;

    auto setRemainingTimeout = [&]
    {
        const auto remaining = (deadline - esp_timer_get_time()) / 1000;
        return remaining > 0 && esp_http_client_set_timeout_ms(client.h, static_cast<int>(std::min<int64_t>(remaining, 10000))) == ESP_OK;
    };

    std::array<char, 72> authorization{};
    std::snprintf(authorization.data(), authorization.size(), "Bearer %s", c.token.data());
    if (esp_http_client_set_header(client.h, "Authorization", authorization.data()) != ESP_OK || esp_http_client_set_header(client.h, "Accept", "application/json") != ESP_OK) return false;
    if (esp_http_client_set_header(client.h, "X-Profile-Name", config::kProfileName) != ESP_OK) return false;

    const auto openError = esp_http_client_open(client.h, 0);
    if (openError != ESP_OK)
    {
        ESP_LOGW("api", "Connection failed: %s", esp_err_to_name(openError));
        return false;
    }

    if (!setRemainingTimeout()) return false;
    int64_t length = esp_http_client_fetch_headers(client.h);
    const auto status = esp_http_client_get_status_code(client.h);
    ESP_LOGI("api", "HTTP status=%d response_length=%lld", status, (long long)length);
    if (length < 0 || length > 4096 || status != 200)
    {
        ESP_LOGW("api", "Response rejected: HTTP=%d length=%lld", status, (long long)length);
        return false;
    }

    std::array<char, 4097> buffer{};
    std::size_t used = 0;
    while (used < buffer.size() - 1)
    {
        if (!setRemainingTimeout()) return false;
        int n = esp_http_client_read(client.h, buffer.data() + used, buffer.size() - 1 - used);
        if (n < 0) return false;
        if (!n) break;
        used += n;
    }

    if (!esp_http_client_is_complete_data_received(client.h) || used == 0 || std::memchr(buffer.data(), 0, used)) return false;

    // Verschachtelung vor dem rekursiven cJSON-Parser begrenzen; Klammern in Strings zählen nicht mit.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i < used; ++i)
    {
        char ch = buffer[i];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (ch == '\\')
                escaped = true;
            else if (ch == '"')
                quoted = false;
        }
        else if (ch == '"')
            quoted = true;
        else if (ch == '{' || ch == '[')
        {
            if (++depth > 8) return false;
        }
        else if (ch == '}' || ch == ']')
        {
            if (!depth) return false;
            --depth;
        }
    }

    const bool accepted = depth == 0 && !quoted && parseWeekly(buffer.data(), state, std::time(nullptr));
    if (!accepted) ESP_LOGW("api", "Weekly response failed validation");
    ESP_LOGI("api", "Weekly response accepted=%d", accepted);
    return accepted;
}
