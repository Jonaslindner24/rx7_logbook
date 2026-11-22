#pragma once
#include "hardware/gps_reader.hpp"
#include "hardware/oled_driver.hpp"
#include <string>

enum class SecondaryScreenMode {
    THERMAL_WARMUP,
    PREMIX_CALC,
    NIGHT_STEALTH
};

struct TelemetrySnapshot {
    double coolant_c{0.0};
    double oil_c{0.0};
    double fuel_liters{30.0};
    std::string time_hm{"00:00"};
    GpsFix gps{};
    int remaining_historic_days{120};
};

class SecondaryScreenRenderer {
public:
    static void render(OledDisplay& display, SecondaryScreenMode mode, const TelemetrySnapshot& data);
    static double calcPremixMl(double fuel_liters, double ratio);
};