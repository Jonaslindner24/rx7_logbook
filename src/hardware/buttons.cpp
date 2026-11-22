#include "hardware/buttons.hpp"
#include <chrono>
#include <iostream>
#include <thread>

#ifndef MOCK_HARDWARE
#include <gpiod.h>

struct InputController::Impl {
    gpiod_chip* chip{nullptr};
    gpiod_line* line_trip{nullptr};
    gpiod_line* line_mode{nullptr};
    gpiod_line* line_power_ok{nullptr};
};
#endif

InputController::InputController(const std::string& chip_name, int pin_trip, int pin_mode, int pin_power_ok)
    : chip_name_(chip_name), pin_trip_(pin_trip), pin_mode_(pin_mode), pin_power_ok_(pin_power_ok) {}

InputController::~InputController() {
#ifndef MOCK_HARDWARE
    if (impl_) {
        if (impl_->line_trip) gpiod_line_release(impl_->line_trip);
        if (impl_->line_mode) gpiod_line_release(impl_->line_mode);
        if (impl_->line_power_ok) gpiod_line_release(impl_->line_power_ok);
        if (impl_->chip) gpiod_chip_close(impl_->chip);
        delete impl_;
    }
#endif
}

bool InputController::init() {
#ifndef MOCK_HARDWARE
    impl_ = new Impl{};
    impl_->chip = gpiod_chip_open_by_name(chip_name_.c_str());
    if (!impl_->chip) {
        std::cerr << "[INPUT] Konnte gpiod-Chip " << chip_name_ << " nicht oeffnen\n";
        return false;
    }

    auto request_input = [&](int offset, const char* consumer) -> gpiod_line* {
        gpiod_line* line = gpiod_chip_get_line(impl_->chip, static_cast<unsigned int>(offset));
        if (!line) return nullptr;
        if (gpiod_line_request_input_flags(line, consumer, GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP) < 0) {
            return nullptr;
        }
        return line;
    };

    impl_->line_trip = request_input(pin_trip_, "rx7_btn_trip");
    impl_->line_mode = request_input(pin_mode_, "rx7_btn_mode");
    impl_->line_power_ok = request_input(pin_power_ok_, "rx7_ups_ok");

    if (!impl_->line_trip || !impl_->line_mode || !impl_->line_power_ok) {
        std::cerr << "[INPUT] Fehler beim Anfordern der GPIO-Leitungen\n";
        return false;
    }
#endif
    return true;
}

InputEvents InputController::poll() {
    InputEvents ev{};

#ifndef MOCK_HARDWARE
    if (!impl_ || !impl_->line_trip || !impl_->line_mode || !impl_->line_power_ok) {
        return ev;
    }

    // Aktiv LOW (Pull-Up auf 3.3V, Taster schaltet nach GND)
    bool trip_low = (gpiod_line_get_value(impl_->line_trip) == 0);
    bool mode_low = (gpiod_line_get_value(impl_->line_mode) == 0);
    // Wenn UPS-Signal auf LOW geht -> Zündung aus / Puffer aktiv -> sauber herunterfahren
    bool power_ok = (gpiod_line_get_value(impl_->line_power_ok) == 1);

    if (trip_low && !last_trip_low_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        if (gpiod_line_get_value(impl_->line_trip) == 0) {
            ev.trip_button_pressed = true;
        }
    }
    if (mode_low && !last_mode_low_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(35));
        if (gpiod_line_get_value(impl_->line_mode) == 0) {
            ev.mode_button_pressed = true;
        }
    }

    last_trip_low_ = trip_low;
    last_mode_low_ = mode_low;

    if (!power_ok) {
        ev.shutdown_requested = true;
    }
#endif

    return ev;
}