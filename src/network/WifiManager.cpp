#include "WifiManager.hpp"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "http_parser.h"
#include "lwip/netdb.h"
#include <string>
#include <cstring>
#include <ctime>

namespace
{
    constexpr EventBits_t kIpv4Ready = BIT0;
    constexpr EventBits_t kIpv6Ready = BIT1;
    constexpr EventBits_t kDisconnected = BIT2;
    constexpr auto kConnectAttemptWait = pdMS_TO_TICKS(8000);
    constexpr auto kRetryDelay = pdMS_TO_TICKS(350);
    constexpr unsigned kConnectAttempts = 2;
    constexpr int64_t kClockResyncSeconds = 24 * 60 * 60;
    RTC_DATA_ATTR int64_t lastClockSync = 0;
}

void WifiManager::event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    auto *self = static_cast<WifiManager *>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED)
    {
        ESP_LOGI("network", "Wi-Fi associated; awaiting IP addresses");
        xEventGroupClearBits(self->events_, kDisconnected);
        esp_netif_create_ip6_linklocal(self->netif_);
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGI("network", "Wi-Fi disconnected: reason=%u", disconnected->reason);
        xEventGroupClearBits(self->events_, kIpv4Ready | kIpv6Ready);
        xEventGroupSetBits(self->events_, kDisconnected);
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        const auto *ip = static_cast<ip_event_got_ip_t *>(data);
        if (ip->esp_netif == self->netif_)
        {
            ESP_LOGI("network", "IPv4 ready");
            xEventGroupSetBits(self->events_, kIpv4Ready);
        }
    }
    else if (base == IP_EVENT && id == IP_EVENT_GOT_IP6)
    {
        auto *ip = static_cast<ip_event_got_ip6_t *>(data);
        const auto type = esp_netif_ip6_get_addr_type(&ip->ip6_info.ip);
        if (ip->esp_netif == self->netif_ && (type == ESP_IP6_ADDR_IS_GLOBAL || type == ESP_IP6_ADDR_IS_UNIQUE_LOCAL))
        {
            ESP_LOGI("network", "Usable IPv6 ready");
            xEventGroupSetBits(self->events_, kIpv6Ready);
        }
    }
}

bool WifiManager::connect(const DeviceCredentials &credentials)
{
    events_ = xEventGroupCreate();
    if (!events_) return false;
    netif_ = esp_netif_create_default_wifi_sta();
    if (!netif_) return false;

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init) != ESP_OK) return false;
    initialized_ = true;

    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, event, this, &ipHandler_) != ESP_OK ||
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, this, &wifiHandler_) != ESP_OK)
        return false;

    wifi_config_t config{};
    std::memcpy(config.sta.ssid, credentials.ssid.data(), std::strlen(credentials.ssid.data()));
    std::memcpy(config.sta.password, credentials.password.data(), std::strlen(credentials.password.data()));
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    config.sta.failure_retry_cnt = 1;
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK || esp_wifi_start() != ESP_OK) return false;
    started_ = true;

    for (unsigned attempt = 0; attempt < kConnectAttempts; ++attempt)
    {
        ESP_LOGI("network", "Wi-Fi attempt %u/%u", attempt + 1, kConnectAttempts);
        xEventGroupClearBits(events_, kIpv4Ready | kIpv6Ready | kDisconnected);
        if (esp_wifi_connect() == ESP_OK)
        {
            const auto result = xEventGroupWaitBits(events_, kIpv4Ready | kIpv6Ready | kDisconnected, pdFALSE, pdFALSE, kConnectAttemptWait);
            ESP_LOGI("network", "IP wait: ipv4=%d ipv6=%d disconnected=%d", !!(result & kIpv4Ready), !!(result & kIpv6Ready), !!(result & kDisconnected));
            if (result & (kIpv4Ready | kIpv6Ready)) return waitForEndpoint(credentials.endpoint.data());
        }

        if (!(xEventGroupGetBits(events_) & kDisconnected))
        {
            esp_wifi_disconnect();
            xEventGroupWaitBits(events_, kDisconnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(500));
        }

        if (attempt + 1 < kConnectAttempts) vTaskDelay(kRetryDelay);
    }
    return false;
}

bool WifiManager::waitForEndpoint(const char *url)
{
    http_parser_url parsed{};
    if (http_parser_parse_url(url, std::strlen(url), 0, &parsed) != 0 || !(parsed.field_set & (1 << UF_HOST))) return false;

    const auto &field = parsed.field_data[UF_HOST];
    const std::string host(url + field.off, field.len);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *address = nullptr;
    const auto dnsStatus = getaddrinfo(host.c_str(), nullptr, &hints, &address);
    if (dnsStatus != 0 || !address)
    {
        ESP_LOGW("network", "API DNS failed: status=%d", dnsStatus);
        return false;
    }

    // ESP-TLS nutzt den ersten DNS-Treffer. Bei IPv6 auf eine nutzbare SLAAC-Adresse warten; DHCP allein reicht nicht.
    const EventBits_t required = address->ai_family == AF_INET ? kIpv4Ready : address->ai_family == AF_INET6 ? kIpv6Ready : 0;
    freeaddrinfo(address);
    if (!required) return false;

    ESP_LOGI("network", "API DNS selects IPv%d; waiting for matching local address", required == kIpv6Ready ? 6 : 4);
    const auto result = xEventGroupWaitBits(events_, required | kDisconnected, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    const bool ready = (result & required) && !(result & kDisconnected);
    ESP_LOGI("network", "API address ready=%d", ready);
    return ready;
}

bool WifiManager::syncClock()
{
    const auto before = time(nullptr);
    const bool rtcUsable = before > 1767225600 && before < 4102444800LL;

    // Die RTC läuft im Schlaf weiter. Eine gültige Uhrzeit nur einmal täglich per NTP nachstellen.
    if (rtcUsable && lastClockSync > 0 && before >= lastClockSync && before - lastClockSync < kClockResyncSeconds)
    {
        return true;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    bool synced = false;
    const unsigned polls = rtcUsable ? 30 : 100; // Mit gültiger RTC 3 s warten, beim Kaltstart 10 s.
    for (unsigned i = 0; i < polls; ++i)
    {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED)
        {
            synced = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    esp_sntp_stop();

    const auto now = time(nullptr);
    const bool usable = now > 1767225600 && now < 4102444800LL;
    if (synced && usable) lastClockSync = now;

    return usable;
}

WifiManager::~WifiManager()
{
    if (wifiHandler_) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifiHandler_);
    if (ipHandler_) esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ipHandler_);
    if (started_)
    {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    if (initialized_) esp_wifi_deinit();
    if (netif_) esp_netif_destroy_default_wifi(netif_);
    if (events_) vEventGroupDelete(events_);
}
