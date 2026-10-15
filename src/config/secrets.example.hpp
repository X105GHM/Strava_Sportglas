#pragma once

// Als secrets.local.hpp kopieren und dort die eigenen Zugangsdaten eintragen.
// Die lokale Datei wird von Git ignoriert. Änderungen erfordern einen neuen Build und Upload.
namespace local_secrets
{
    inline constexpr bool kEnabled = true; // false: Zugangsdaten aus NVS verwenden.
    inline constexpr char kSsid[] = "";
    inline constexpr char kPassword[] = "";
    inline constexpr char kEndpoint[] = "https://sport.x105ghm.com/api/v1/glass/weekly";
    inline constexpr char kDeviceToken[] = ""; // DEVICE_TOKEN_CURRENT aus dem CT, 64 Hexzeichen.
}
