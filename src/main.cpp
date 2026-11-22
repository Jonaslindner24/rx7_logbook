#include "config.hpp"
#include "hardware/gps_reader.hpp"
#include "hardware/oled_driver.hpp"
#include "storage/logbook.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace {

enum class SystemState {
    STANDBY,
    DRIVING
};

std::string currentDateIso() {
    std::time_t t = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&t, &tm_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%d");
    return oss.str();
}

std::string currentTimeHm() {
    std::time_t t = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&t, &tm_now);
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%H:%M");
    return oss.str();
}

} // namespace

int main() {
    OledDisplay oled_left(rx7::cfg::OLED_PRIMARY_ADDR, "DISP1_LEFT");
    OledDisplay oled_right(rx7::cfg::OLED_SECONDARY_ADDR, "DISP2_RIGHT");
    oled_left.init();
    oled_right.init();

    Logbook logbook("./fahrtenbuch_test.csv");
    GpsReader gps;

    std::atomic<bool> running{true};
    std::atomic<SystemState> state{SystemState::STANDBY};

    // Animations- und Render-Thread (ca. 30 FPS im Standby)
    std::thread render_thread([&]() {
        int angle = 0;
        while (running.load()) {
            oled_left.clear();
            oled_right.clear();

            if (state.load() == SystemState::STANDBY) {
                // Kreisbahn + rotierender Punkt als Platzhalter für RX-7 FC Sprite
                oled_left.drawCircle(64, 32, 20, true);
                double rad = angle * (M_PI / 180.0);
                int car_x = 64 + static_cast<int>(std::lround(20.0 * std::cos(rad)));
                int car_y = 32 + static_cast<int>(std::lround(20.0 * std::sin(rad)));
                oled_left.setPixel(car_x, car_y, true);
                oled_left.setPixel(car_x + 1, car_y, true);
                oled_left.setPixel(car_x, car_y + 1, true);

                angle = (angle + 12) % 360;
                oled_left.present();
                oled_right.present();
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            } else {
                oled_left.drawCircle(20, 32, 6, true);
                oled_left.present();
                oled_right.present();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    });

    // Beispiel-Testlauf für 2 Sekunden und sauberer Abschluss
    std::this_thread::sleep_for(std::chrono::seconds(2));

    GpsFix start_fix = gps.poll(rx7::cfg::GPS_DEVICE);
    state.store(SystemState::DRIVING);
    std::string t_start = currentTimeHm();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    GpsFix end_fix = gps.poll(rx7::cfg::GPS_DEVICE);
    double dist = (start_fix.valid && end_fix.valid)
        ? GpsReader::haversineKm(start_fix.lat, start_fix.lon, end_fix.lat, end_fix.lon)
        : 0.0;

    TripRecord rec{
        currentDateIso(),
        t_start,
        currentTimeHm(),
        start_fix.lat,
        start_fix.lon,
        end_fix.lat,
        end_fix.lon,
        dist,
        ""
    };
    logbook.appendTrip(rec);
    state.store(SystemState::STANDBY);

    running.store(false);
    render_thread.join();
    std::cout << "Fahrt gespeichert. Historische Tage heuer: "
              << logbook.usedHistoricDays(2026) << "/" << rx7::cfg::MAX_HISTORIC_DAYS_PER_YEAR << "\n";
    return 0;
}