#pragma once
#include "esp_err.h"
#include <cstdint>

class PowerManager
{
public:
    static bool validPins();
    void begin(bool retainedOn);
    void enable();
    void outputEnable(bool enabled);
    // Nach der RMT-Freigabe und vor dem Schlafen aufrufen.
    void parkData();
    void off();
    // Kehrt nach Light Sleep zurück und gibt die GPIO-Holds wieder frei.
    esp_err_t sleep(bool powered, uint64_t seconds);
};
