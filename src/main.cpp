#include "config.hpp"
#include "hardware/buttons.hpp"
#include "hardware/gps_reader.hpp"
#include "hardware/oled_driver.hpp"
#include "storage/logbook.hpp"
#include "storage/sync_server.hpp"
#include "ui/screens.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace
{

    enum class SystemState
    {
        STANDBY,
        DRIVING
    };

    std::string currentDateIso()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm_now{};
        localtime_r(&t, &tm_now);
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%Y-%m-%d");
        return oss.str();
    }

    std::string currentTimeHm()
    {
        std::time_t t = std::time(nullptr);
        std::tm tm_now{};
        localtime_r(&t, &tm_now);
        std::ostringstream oss;
        oss << std::put_time(&tm_now, "%H:%M");
        return oss.str();
    }

    bool isWlanConnected()
    {
#ifdef MOCK_HARDWARE
        return false;
#else
        std::ifstream operstate("/sys/class/net/wlan0/operstate");
        if (!operstate.is_open())
            return false;
        std::string state_str;
        operstate >> state_str;
        return (state_str == "up");
#endif
    }

} // namespace

int main()
{
    OledDisplay oled_left(rx7::cfg::OLED_PRIMARY_ADDR, "DISP1_LEFT");
    OledDisplay oled_right(rx7::cfg::OLED_SECONDARY_ADDR, "DISP2_RIGHT");
    oled_left.init();
    oled_right.init();

    InputController inputs("gpiochip0",
                           rx7::cfg::PIN_BTN_TRIP,
                           rx7::cfg::PIN_BTN_MODE,
                           rx7::cfg::PIN_UPS_POWER_OK);
    inputs.init();

    Logbook logbook(rx7::cfg::CSV_PATH);
    GpsReader gps;
    gps.openPort(rx7::cfg::GPS_DEVICE, 9600);

    SyncServer sync_server(rx7::cfg::CSV_PATH, 8080);
    sync_server.start();

    std::atomic<bool> running{true};
    std::atomic<bool> trigger_shutdown{false};
    std::atomic<SystemState> state{SystemState::STANDBY};
    std::atomic<SecondaryScreenMode> sec_mode{SecondaryScreenMode::THERMAL_WARMUP};

    std::mutex data_mutex;
    TelemetrySnapshot telemetry{};
    telemetry.remaining_historic_days =
        std::max(0, rx7::cfg::MAX_HISTORIC_DAYS_PER_YEAR - logbook.usedHistoricDays(2026));

    // Aktive Fahrt-Daten
    bool has_prev_fix = false;
    GpsFix prev_fix{};
    TripRecord active_trip{};
    double trip_distance_km = 0.0;

    auto finalizeActiveTrip = [&]()
    {
        if (state.load() != SystemState::DRIVING)
            return;
        GpsFix end_fix = gps.poll(rx7::cfg::GPS_DEVICE);
        if (end_fix.valid)
        {
            active_trip.end_lat = end_fix.lat;
            active_trip.end_lon = end_fix.lon;
        }
        else if (has_prev_fix)
        {
            active_trip.end_lat = prev_fix.lat;
            active_trip.end_lon = prev_fix.lon;
        }
        active_trip.end_time = currentTimeHm();
        active_trip.distance_km = trip_distance_km;
        logbook.appendTrip(active_trip);

        std::lock_guard<std::mutex> lk(data_mutex);
        telemetry.remaining_historic_days =
            std::max(0, rx7::cfg::MAX_HISTORIC_DAYS_PER_YEAR - logbook.usedHistoricDays(2026));

        has_prev_fix = false;
        trip_distance_km = 0.0;
        active_trip = TripRecord{};
        state.store(SystemState::STANDBY);
        std::cout << "[TRIP] Fahrt gespeichert und beendet.\n";
    };

    // Render-Thread (ca. 30 FPS)
    std::thread render_thread([&]()
                              {
        int angle = 0;
        while (running.load()) {
            TelemetrySnapshot snap;
            {
                std::lock_guard<std::mutex> lk(data_mutex);
                snap = telemetry;
                snap.time_hm = currentTimeHm();
            }

            oled_left.clear();
            if (state.load() == SystemState::STANDBY) {
                // RX-7 FC Kreisbahn-Animation auf Display 1
                oled_left.drawCircle(64, 32, 20, true);
                double rad = angle * (M_PI / 180.0);
                int car_x = 64 + static_cast<int>(std::lround(20.0 * std::cos(rad)));
                int car_y = 32 + static_cast<int>(std::lround(20.0 * std::sin(rad)));
                oled_left.setPixel(car_x, car_y, true);
                oled_left.setPixel(car_x + 1, car_y, true);
                oled_left.setPixel(car_x, car_y + 1, true);
                angle = (angle + 12) % 360;
            } else {
                // Aktive Fahrt: Status-Markierung + Distanz-Balken
                oled_left.drawCircle(16, 32, 6, true);
                double km_ratio = std::min(1.0, snap.gps.speed_kmh / 140.0);
                int bar_w = static_cast<int>(std::lround(km_ratio * 80.0));
                for (int y = 28; y <= 36; ++y) {
                    for (int x = 30; x < 30 + bar_w; ++x) {
                        oled_left.setPixel(x, y, true);
                    }
                }
            }

            SecondaryScreenRenderer::render(oled_right, sec_mode.load(), snap);

            oled_left.present();
            oled_right.present();

            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        } });

    // Haupt-Steuerloop (Taster, GPS, WLAN-Sync, Shutdown)
    auto last_gps_poll = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    int loop_ticks = 0;

#ifdef MOCK_HARDWARE
    const int max_mock_ticks = 60; // ca. 3 Sekunden Testlauf im Mock-Modus
#endif

    while (running.load())
    {
        InputEvents ev = inputs.poll();

        if (ev.shutdown_requested)
        {
            trigger_shutdown.store(true);
            running.store(false);
            break;
        }

        if (ev.trip_button_pressed)
        {
            GpsFix now_fix = gps.poll(rx7::cfg::GPS_DEVICE);
            if (state.load() == SystemState::STANDBY)
            {
                active_trip = TripRecord{};
                active_trip.date = currentDateIso();
                active_trip.start_time = currentTimeHm();
                if (now_fix.valid)
                {
                    active_trip.start_lat = now_fix.lat;
                    active_trip.start_lon = now_fix.lon;
                    prev_fix = now_fix;
                    has_prev_fix = true;
                }
                trip_distance_km = 0.0;
                state.store(SystemState::DRIVING);
                std::cout << "[TRIP] Fahrt gestartet am " << active_trip.date
                          << " um " << active_trip.start_time << "\n";
            }
            else
            {
                // Zwischenstopp protokollieren
                std::ostringstream stop_entry;
                stop_entry << currentTimeHm();
                if (now_fix.valid)
                {
                    stop_entry << "@" << std::fixed << std::setprecision(4)
                               << now_fix.lat << "," << now_fix.lon;
                }
                if (!active_trip.stops.empty())
                    active_trip.stops += " | ";
                active_trip.stops += stop_entry.str();
                std::cout << "[TRIP] Zwischenstopp erfasst: " << stop_entry.str() << "\n";
            }
        }

        if (ev.mode_button_pressed)
        {
            SecondaryScreenMode cur = sec_mode.load();
            if (cur == SecondaryScreenMode::THERMAL_WARMUP)
            {
                sec_mode.store(SecondaryScreenMode::PREMIX_CALC);
            }
            else if (cur == SecondaryScreenMode::PREMIX_CALC)
            {
                // Im Pre-Mix-Modus Literzahl in 5-L-Schritten erhöhen (10..60 L)
                std::lock_guard<std::mutex> lk(data_mutex);
                telemetry.fuel_liters += 5.0;
                if (telemetry.fuel_liters > 60.0)
                {
                    telemetry.fuel_liters = 10.0;
                    sec_mode.store(SecondaryScreenMode::NIGHT_STEALTH);
                }
            }
            else
            {
                sec_mode.store(SecondaryScreenMode::THERMAL_WARMUP);
            }
        }

        auto now_tp = std::chrono::steady_clock::now();
        if (now_tp - last_gps_poll >= std::chrono::seconds(1))
        {
            last_gps_poll = now_tp;
            GpsFix fix = gps.poll(rx7::cfg::GPS_DEVICE);

            if (state.load() == SystemState::DRIVING && fix.valid)
            {
                if (has_prev_fix)
                {
                    double step_km = GpsReader::haversineKm(prev_fix.lat, prev_fix.lon, fix.lat, fix.lon);
                    if (step_km >= 0.005 && step_km < 5.0)
                    {
                        trip_distance_km += step_km;
                    }
                }
                prev_fix = fix;
                has_prev_fix = true;
            }

            {
                std::lock_guard<std::mutex> lk(data_mutex);
                telemetry.gps = fix;
            }

            // Heimkehr ins WLAN beendet aktive Fahrt automatisch
            if (state.load() == SystemState::DRIVING && isWlanConnected())
            {
                std::cout << "[WLAN] Heim-WLAN erkannt -> Fahrt wird abgeschlossen.\n";
                finalizeActiveTrip();
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));

#ifdef MOCK_HARDWARE
        ++loop_ticks;
        if (loop_ticks >= max_mock_ticks)
        {
            running.store(false);
        }
#else
        (void)loop_ticks;
#endif
    }

    finalizeActiveTrip();
    render_thread.join();
    sync_server.stop();
    gps.closePort();

    if (trigger_shutdown.load())
    {
#ifndef MOCK_HARDWARE
        std::cout << "[PWR] Shutdown wird eingeleitet...\n";
        (void)std::system("/sbin/shutdown -h now");
#endif
    }

    return 0;
}