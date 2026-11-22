#pragma once
#include <cstdint>
#include <string>

namespace rx7::cfg
{

    // I2C-Adressen auf /dev/i2c-1
    constexpr const char *I2C_BUS_PATH = "/dev/i2c-1";
    constexpr uint8_t OLED_PRIMARY_ADDR = 0x3C;
    constexpr uint8_t OLED_SECONDARY_ADDR = 0x3D;
    constexpr int OLED_WIDTH = 128;
    constexpr int OLED_HEIGHT = 64;

    // GPIO-Pins (BCM-Nummerierung für Raspberry Pi 3B)
    constexpr int PIN_BTN_TRIP = 21;     // Taster 1: Start / Zwischenstopp / Ende
    constexpr int PIN_BTN_MODE = 20;     // Taster 2: Display 2 Ansicht / Pre-Mix
    constexpr int PIN_UPS_POWER_OK = 26; // Signal von USV/Zündung (LOW = Zündung aus)

    // Fahrzeug & Österreich Oldtimer-Fahrtenbuch
    constexpr int MAX_HISTORIC_DAYS_PER_YEAR = 120;
    constexpr double PREMIX_RATIO = 200.0; // 1:200 (ml Öl = Liter Benzin * 1000 / 200)
    constexpr double WARMUP_COOLANT_THRESHOLD_C = 70.0;

    // Speicherpfade
    constexpr const char *DB_PATH = "/var/lib/rx7/fahrtenbuch.db";
    constexpr const char *CSV_PATH = "/var/lib/rx7/fahrtenbuch.csv";
    constexpr const char *GPS_DEVICE = "/dev/ttyACM0";

} // namespace rx7::cfg