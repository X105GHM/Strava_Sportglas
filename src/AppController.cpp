#include "AppController.hpp"
#include "display/AnimationController.hpp"
#include "input/TouchController.hpp"
#include "network/FetchWorker.hpp"
#include "power/PowerManager.hpp"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#ifdef CONFIG_DISPLAY_SERIAL_DEBUG
#ifdef CONFIG_IDF_TARGET_ESP32
#include "driver/uart.h"
#else
#include "driver/usb_serial_jtag.h"
#endif
#endif
#ifdef STRAVA_TOUCH_DIAGNOSTICS
#include <cstdio>
#endif

void AppController::run()
{
    if (!PowerManager::validPins())
    {
        ESP_LOGE("app", "Board/pin configuration is not verified");
        return;
    }

    PersistentState store;
    auto saved = store.load(); // Letzten gültigen Datensatz auch über den Wochenwechsel behalten.
    #ifdef CONFIG_DISPLAY_SERIAL_DEBUG
    #ifdef CONFIG_IDF_TARGET_ESP32
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, nullptr, 0));
    #else
    usb_serial_jtag_driver_config_t serialConfig{};
    serialConfig.rx_buffer_size = 256;
    serialConfig.tx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&serialConfig));
    #endif
    auto status = [&]
    {
        ESP_LOGI("app", "mode=%u has_summary=%d ride=%lu/%lu m", static_cast<unsigned>(saved.mode), saved.hasSummary, static_cast<unsigned long>(saved.summary.ride.currentMeters),
            static_cast<unsigned long>(saved.summary.ride.averageWeekMeters));
    };
    ESP_LOGI("app", "SERIAL DEBUG: sleep disabled. s=status n=refresh m=next mode (0=off 1=sport 2=white)");
    status();
    #endif

    PowerManager power;
    bool powered = false;
    power.begin(powered);

    TouchController touch;
    uint32_t storedTouchBaseline = config::kPersistTouchCalibration ? store.loadTouchBaseline() : 0;
    const auto touchError = touch.begin(storedTouchBaseline, false);
    bool touchReady = touchError == ESP_OK;
    #ifdef STRAVA_TOUCH_DIAGNOSTICS
    std::printf("TOUCH boot mode=%u baseline=%lu status=%s\n", static_cast<unsigned>(saved.mode), static_cast<unsigned long>(touch.baseline()), esp_err_to_name(touchError));
    #endif
    ESP_LOGI("app", "Touch initialized: %s", esp_err_to_name(touchError));

    LedStrip leds;
    DisplayRenderer renderer;
    DisplayRenderer::Frame displayedFrame{};
    bool sportDisplayed = false;

    FetchWorker worker;
    bool fetching = false;
    int64_t fetchStartedUs = 0;

    // Die monotone Zeit läuft im Light Sleep weiter; NTP verstellt diesen Abrufplan nicht.
    int64_t nextFetchUs = 0;
    int64_t idleUntilUs = esp_timer_get_time() + int64_t(config::kInteractionIdleMs) * 1000;
    bool modeDirty = false;
    bool retryRelease = false;

    auto persist = [&]
    {
        const auto err = store.save(saved);
        if (err != ESP_OK) ESP_LOGW("app", "Saving state failed: %s", esp_err_to_name(err));
    };

    auto advanceMode = [&]
    {
        saved.mode = static_cast<DisplayMode>((static_cast<unsigned>(saved.mode) + 1) % 3);
        #ifdef STRAVA_TOUCH_DIAGNOSTICS
        std::printf("TOUCH mode=%u\n", static_cast<unsigned>(saved.mode));
        #endif
        ESP_LOGI("app", "Display mode=%u", static_cast<unsigned>(saved.mode));
        modeDirty = true;
        idleUntilUs = esp_timer_get_time() + int64_t(config::kInteractionIdleMs) * 1000;
        // Umschalten nutzt den Cache; NVS wird erst nach der Bedienung geschrieben.
    };

    // Vor dem Einschalten von WLAN kalibrieren.
    if (touchReady && !touch.baseline())
        while (touch.prepareSleep() == SleepPreparation::Pending)
            vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
    #ifndef CONFIG_IDF_TARGET_ESP32
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TOUCHPAD);
    #endif

    AppState state = AppState::Render;
    for (;;)
    {
        #ifdef CONFIG_DISPLAY_SERIAL_DEBUG
        char command = 0;
        #ifdef CONFIG_IDF_TARGET_ESP32
        const auto commandBytes = uart_read_bytes(UART_NUM_0, &command, 1, 0);
        #else
        const auto commandBytes = usb_serial_jtag_read_bytes(&command, 1, 0);
        #endif
        if (commandBytes > 0)
        {
            if (command == 's')
                status();
            else if (command == 'm')
            {
                advanceMode();
                state = AppState::Render;
            }
            else if (command == 'n' && !fetching)
            {
                ESP_LOGI("app", "Manual refresh requested");
                nextFetchUs = 0;
                state = AppState::FetchData;
            }
        }
        #endif

        switch (state)
        {
        case AppState::FetchData:
        {
            if (!fetching)
            {
                DeviceCredentials credentials;
                fetching = store.credentials(credentials) && worker.start(credentials, saved);
                if (fetching) fetchStartedUs = esp_timer_get_time();
                ESP_LOGI("app", "Refresh started=%d", fetching);

                if (!fetching)
                {
                    ESP_LOGW("app", "Refresh could not start; retaining cache");
                    nextFetchUs = esp_timer_get_time() + int64_t(config::kFetchRetrySeconds) * 1000000;
                    state = AppState::PrepareSleep;
                    break;
                }
            }

            FetchWorker::Result result;
            if (worker.poll(result))
            {
                fetching = false;
                #ifdef STRAVA_TOUCH_DIAGNOSTICS
                std::printf("TOUCH fetch finished success=%d mode=%u\n", result.success, static_cast<unsigned>(saved.mode));
                #endif
                ESP_LOGI("app", "Refresh finished: success=%d cached_summary=%d", result.success, saved.hasSummary);

                if (result.success)
                {
                    // Während des Abrufs kann per Touch ein anderer Modus gewählt worden sein.
                    result.state.mode = saved.mode;
                    saved = result.state;
                    persist();
                    modeDirty = false;
                    ESP_LOGI("app", "Weekly ride=%lu/%lu m", static_cast<unsigned long>(saved.summary.ride.currentMeters), static_cast<unsigned long>(saved.summary.ride.averageWeekMeters));
                }

                nextFetchUs = esp_timer_get_time() + int64_t(result.success ? config::kWakeSeconds : config::kFetchRetrySeconds) * 1000000;
                state = AppState::Render;
            }
            else if (touchReady && touch.pollPress())
            {
                advanceMode();
                state = AppState::Render;
            }
            else if (esp_timer_get_time() - fetchStartedUs >= int64_t(config::kFetchHardTimeoutSeconds) * 1000000)
            {
                // Den Netzwerk-Task nicht mitten im Treiberzugriff löschen. Ein Neustart gibt die Ressourcen sauber frei.
                ESP_LOGE("app", "Fetch worker exceeded hard timeout; restarting");
                if (modeDirty) persist();
                esp_restart();
            }
            else
                vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
            break;
        }

        case AppState::Render:
        {
            ESP_LOGI("app", "Render mode=%u has_summary=%d", static_cast<unsigned>(saved.mode), saved.hasSummary);

            if (saved.mode == DisplayMode::Off)
            {
                // Erst schwarz senden: Restversorgung über DATA kann sonst LEDs weiterleuchten lassen.
                if (powered)
                {
                    auto err = leds.begin();
                    if (err == ESP_OK)
                    {
                        power.outputEnable(true);
                        err = leds.black();
                    }
                    if (err != ESP_OK) ESP_LOGW("app", "LED black frame failed: %s", esp_err_to_name(err));
                }

                power.outputEnable(false);
                leds.release();
                power.off();
                powered = false;
                sportDisplayed = false;
            }
            else
            {
                // DATA bleibt LOW, bis die 5-V-Versorgung stabil ist.
                if (!powered) power.enable();
                powered = true;

                auto err = leds.begin();
                if (err == ESP_OK)
                {
                    power.outputEnable(true);
                    const auto mode = saved.mode == DisplayMode::Sport && !saved.hasSummary ? DisplayMode::WarmWhite : saved.mode;
                    const auto frame = renderer.render(mode, saved.summary);

                    if (sportDisplayed && mode == DisplayMode::Sport && frame != displayedFrame)
                    {
                        AnimationController animation;
                        const auto result = animation.transition(leds, displayedFrame, frame,
                            [&]
                            {
                                return touchReady && touch.pollPress();
                            });
                        err = result.error;
                        if (result.interrupted)
                        {
                            advanceMode();
                            break;
                        }
                    }
                    else
                    {
                        // Ohne Messwert warmweiß anzeigen; ein gemessener Nullwert ist davon zu unterscheiden.
                        err = leds.show(frame);
                    }

                    if (err == ESP_OK)
                    {
                        displayedFrame = frame;
                        sportDisplayed = mode == DisplayMode::Sport && DisplayRenderer::hasSportColor(frame);
                    }
                }

                if (err != ESP_OK)
                {
                    #ifdef STRAVA_TOUCH_DIAGNOSTICS
                    std::printf("TOUCH LED error=%s\n", esp_err_to_name(err));
                    #endif
                    ESP_LOGE("app", "LED output failed: %s", esp_err_to_name(err));
                    leds.release();
                    power.off();
                    powered = false;
                    sportDisplayed = false;
                }
            }

            // Die LEDs halten das Bild selbst. RMT freigeben und DATA im Leerlauf LOW halten.
            power.outputEnable(false);
            leds.release();
            power.parkData();

            state = fetching ? AppState::FetchData : AppState::PrepareSleep;
            break;
        }

        case AppState::PrepareSleep:
        {
            // Hier ist der Netzwerk-Task mit dem Abbau seiner Ressourcen fertig.
            if (esp_timer_get_time() < idleUntilUs)
            {
                if (touchReady && touch.pollPress())
                {
                    advanceMode();
                    state = AppState::Render;
                }
                else
                {
                    vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
                }
                break;
            }

            const auto preparation = touchReady ? touch.prepareSleep() : SleepPreparation::Retry;
            if (preparation == SleepPreparation::Pending)
            {
                vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
                break;
            }

            if (preparation == SleepPreparation::NewPress)
            {
                advanceMode();
                state = AppState::Render;
                break;
            }

            retryRelease = preparation == SleepPreparation::Retry;
            if (modeDirty)
            {
                persist();
                modeDirty = false;
            }

            if (config::kPersistTouchCalibration && preparation == SleepPreparation::Ready && !storedTouchBaseline)
            {
                if (store.saveTouchBaseline(touch.baseline()) == ESP_OK) storedTouchBaseline = touch.baseline();
            }

            state = esp_timer_get_time() >= nextFetchUs ? AppState::FetchData : AppState::Sleep;
            break;
        }

        case AppState::Sleep:
        {
            #ifdef CONFIG_DISPLAY_SERIAL_DEBUG
            if (touchReady && touch.pollPress())
            {
                advanceMode();
                state = AppState::Render;
            }
            else if (esp_timer_get_time() >= nextFetchUs)
            {
                state = AppState::FetchData;
            }
            else
            {
                vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
            }
            break;
            #endif

            const auto untilFetch = nextFetchUs - esp_timer_get_time();
            // Kurze Timer-Wakeups prüfen den Sensor; der API-Termin bleibt dabei unverändert.
            auto remaining = std::min<int64_t>(std::max<int64_t>(1, (untilFetch + 999999) / 1000000), config::kTouchHealthSeconds);
            if (retryRelease) remaining = std::min<int64_t>(remaining, config::kFalseTouchCooldownSeconds);
            #ifdef STRAVA_TOUCH_DIAGNOSTICS
            std::printf("TOUCH sleep mode=%u seconds=%lld baseline=%lu retry=%d\n", static_cast<unsigned>(saved.mode), static_cast<long long>(remaining), static_cast<unsigned long>(touch.baseline()), retryRelease);
            #endif
            #ifdef STRAVA_SLEEP_SELF_TEST
            static unsigned cycle = 0;
            if (cycle < 12) remaining = 1;
            #endif

            const auto err = power.sleep(powered, remaining);
            // Bei gleichzeitigem Timer- und Touch-Wakeup beide Ursachen auswerten.
            const bool touchWake = err == ESP_OK && (esp_sleep_get_wakeup_causes() & (1U << ESP_SLEEP_WAKEUP_TOUCHPAD));
            touch.resume(touchWake);
            #ifdef STRAVA_TOUCH_DIAGNOSTICS
            std::printf("TOUCH resume cause=%u status=%s mode=%u\n", static_cast<unsigned>(esp_sleep_get_wakeup_cause()), esp_err_to_name(err), static_cast<unsigned>(saved.mode));
            #endif

            if (retryRelease) touchReady = touch.recover() == ESP_OK;
            #ifdef STRAVA_SLEEP_SELF_TEST
            if (err == ESP_OK && esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER && cycle < 12)
            {
                ++cycle;
                std::printf("TOUCH self-test cycle=%u\n", cycle);
                advanceMode();
                state = AppState::Render;
                break;
            }
            #endif

            if (touchWake && touchReady)
            {
                advanceMode();
                state = AppState::Render;
            }
            else
            {
                // Abgewiesenen Sleep im Wachbetrieb entprellen; die alte Wake-Ursache zählt nicht erneut.
                if (err != ESP_OK) vTaskDelay(pdMS_TO_TICKS(config::kTouchSampleMs));
                state = AppState::PrepareSleep;
            }
            break;
        }

        default:
            state = AppState::Render;
            break;
        }
    }
}
