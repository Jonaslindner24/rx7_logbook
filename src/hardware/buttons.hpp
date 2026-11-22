#pragma once
#include <cstdint>
#include <string>

struct InputEvents {
    bool trip_button_pressed{false}; // Taster 1: Fahrt Start / Zwischenstopp / Ende
    bool mode_button_pressed{false}; // Taster 2: Display-2-Ansicht / Pre-Mix-Wert
    bool shutdown_requested{false};  // USV/Zündung meldet Stromausfall / Zündung AUS
};

class InputController {
public:
    InputController(const std::string& chip_name, int pin_trip, int pin_mode, int pin_power_ok);
    ~InputController();

    bool init();
    InputEvents poll();

private:
    std::string chip_name_;
    int pin_trip_;
    int pin_mode_;
    int pin_power_ok_;
    bool last_trip_low_{false};
    bool last_mode_low_{false};

#ifndef MOCK_HARDWARE
    struct Impl;
    Impl* impl_{nullptr};
#endif
};