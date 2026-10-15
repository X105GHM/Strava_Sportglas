# Sportglas

Sportglas displays weekly cycling distance on an illuminated glass panel.
Three LEDs indicate progress relative to the average weekly distance:
red at 0%, yellow at 50%, and green at 100% or above. The remaining LEDs
provide a warm white background. Activity data is retrieved over Wi-Fi
from an HTTPS API.

## Hardware

The board uses an ESP32-PICO-MINI-02-N8R2 with 8 MB of flash.
The onboard PSRAM is not used. The strip contains 43 WS2812 LEDs
with GRB color order.

| Function | Connection |
|---|---|
| LED data | GPIO21 |
| LED 5 V supply enable, HIGH = on | GPIO19 |
| Touch electrode, channel T0 | GPIO4 |
| AHCT buffer OE# | Permanently connected to GND |
| UART TX / RX, 3.3 V logic | GPIO1 / GPIO3 |

The cycling indicator uses LEDs 20–22, numbered from 1 at the DIN end.
The firmware applies brightness adjustment, gamma correction, and an estimated
900 mA LED current limit. The power supply must support the actual load.

## Controls and display

Each touch cycles through `OFF → SPORT → WARM WHITE → OFF`.
Holding the pad counts as one touch; release it before touching again.
Leave the pad untouched during startup calibration.

SPORT uses the most recently saved values. Until a valid dataset is available,
the display remains warm white. If a valid dataset contains zero for both
the current distance and the comparison value, the sport zone remains dark.

When new API data changes an already visible sport color, the display fades
to the new color over 800 ms. The background remains unchanged.
A touch can interrupt the transition.

The selected mode is saved after interaction has ended. Disconnecting power
immediately after switching modes may therefore leave the previous mode saved.

## Data retrieval and sleep

The firmware requests data after startup and then once per hour. After a failed
request, it retries after five minutes. Switching modes does not trigger an
additional request. Invalid responses and connection failures leave the last
valid dataset intact, including across a change of week.

Wi-Fi, clock synchronization, and HTTPS run in a separate FreeRTOS task while
touch controls remain responsive. TLS verifies the certificate and hostname.
API responses are checked for valid structure, values, and timestamps before
being accepted. If the network task stalls for more than 90 seconds,
the ESP restarts.

After a touch, the ESP stays awake for two seconds and samples the pad every
20 ms. It then enters light sleep with touch wakeup. A brief timer wakeup
checks the sensor every 30 seconds without triggering an additional API request.
Light sleep consumes more standby current than deep sleep.

During sleep, the LED supply state is held according to the selected mode,
and DATA is held LOW. GPIO holds are released after wakeup. When enabling
the LED supply, the firmware waits 50 ms before transmitting data.
LED transmission has a 100 ms timeout.

Regular application and bootloader logs are disabled. The ESP32 ROM may still
print a startup message over UART after a reset.

## Configuration and API

Wi-Fi and API credentials are stored in `src/config/secrets.local.hpp`.
This file is ignored by Git. `src/config/secrets.example.hpp` describes
the required entries.

| Entry | Meaning |
|---|---|
| `kSsid` | Wi-Fi network name |
| `kPassword` | Wi-Fi password |
| `kEndpoint` | Full HTTPS URL of the weekly API |
| `kDeviceToken` | Bearer device token containing 64 hexadecimal characters |
| `kEnabled` | `true`: local credentials; `false`: credentials from NVS |

The profile name is defined as `kProfileName` in `src/config/Config.hpp`
and sent in the `X-Profile-Name` header. The current profile is `Danilo`.
The endpoint is `https://sport.x105ghm.com/api/v1/glass/weekly`.

Pin assignments, display mode settings, and ESP-IDF defaults are defined
in `sdkconfig.defaults`. LED zones, colors, timing, and touch thresholds
are defined in `src/config/Config.hpp`. Currently, only cycling is enabled.

## Firmware structure

The firmware uses ESP-IDF 5.5.3 and C++20. Project settings are stored in
`platformio.ini`; the flash layout, including NVS, is defined in `partitions.csv`.

| File or directory | Purpose |
|---|---|
| `src/app_main.cpp` | NVS and network infrastructure initialization |
| `src/AppController.cpp` | Mode switching, request scheduling, display, and sleep control |
| `src/display/` | Color mapping, transitions, and LED output through RMT |
| `src/input/` | Touch measurement, calibration, and debouncing |
| `src/network/` | Wi-Fi, HTTPS requests, and API response validation |
| `src/storage/` | Mode, dataset, and credentials in NVS |
| `src/power/` | LED supply, GPIO holds, and light sleep |
| `src/models/` | Shared data structures |

This README was created with AI assistance.
