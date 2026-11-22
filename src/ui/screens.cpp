#include "ui/screens.hpp"
#include "config.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

// Einfacher 3x5 Pixel-Bitmap-Zeichensatz für Ziffern und wenige Großbuchstaben
void drawGlyph3x5(OledDisplay& d, int x, int y, char ch) {
    static const uint8_t digit_0[5] = {0b111, 0b101, 0b101, 0b101, 0b111};
    static const uint8_t digit_1[5] = {0b010, 0b110, 0b010, 0b010, 0b111};
    static const uint8_t digit_2[5] = {0b111, 0b001, 0b111, 0b100, 0b111};
    static const uint8_t digit_3[5] = {0b111, 0b001, 0b111, 0b001, 0b111};
    static const uint8_t digit_4[5] = {0b101, 0b101, 0b111, 0b001, 0b001};
    static const uint8_t digit_5[5] = {0b111, 0b100, 0b111, 0b001, 0b111};
    static const uint8_t digit_6[5] = {0b111, 0b100, 0b111, 0b101, 0b111};
    static const uint8_t digit_7[5] = {0b111, 0b001, 0b010, 0b010, 0b010};
    static const uint8_t digit_8[5] = {0b111, 0b101, 0b111, 0b101, 0b111};
    static const uint8_t digit_9[5] = {0b111, 0b101, 0b111, 0b001, 0b111};
    static const uint8_t colon_c[5] = {0b000, 0b010, 0b000, 0b010, 0b000};
    static const uint8_t dot_c[5]   = {0b000, 0b000, 0b000, 0b000, 0b010};

    const uint8_t* rows = nullptr;
    switch (ch) {
        case '0': rows = digit_0; break;
        case '1': rows = digit_1; break;
        case '2': rows = digit_2; break;
        case '3': rows = digit_3; break;
        case '4': rows = digit_4; break;
        case '5': rows = digit_5; break;
        case '6': rows = digit_6; break;
        case '7': rows = digit_7; break;
        case '8': rows = digit_8; break;
        case '9': rows = digit_9; break;
        case ':': rows = colon_c; break;
        case '.': rows = dot_c; break;
        default: return;
    }

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (rows[r] & (1 << (2 - c))) {
                d.setPixel(x + c, y + r, true);
            }
        }
    }
}

void drawText(OledDisplay& d, int x, int y, const std::string& text) {
    int cursor_x = x;
    for (char ch : text) {
        drawGlyph3x5(d, cursor_x, y, ch);
        cursor_x += 4;
    }
}

void drawBar(OledDisplay& d, int x, int y, int w, int h, double ratio_0_to_1) {
    double clamped = std::max(0.0, std::min(1.0, ratio_0_to_1));
    int fill_w = static_cast<int>(std::lround(clamped * (w - 2)));

    for (int i = 0; i < w; ++i) {
        d.setPixel(x + i, y, true);
        d.setPixel(x + i, y + h - 1, true);
    }
    for (int j = 0; j < h; ++j) {
        d.setPixel(x, y + j, true);
        d.setPixel(x + w - 1, y + j, true);
    }
    for (int j = 1; j < h - 1; ++j) {
        for (int i = 1; i <= fill_w; ++i) {
            d.setPixel(x + i, y + j, true);
        }
    }
}

} // namespace

double SecondaryScreenRenderer::calcPremixMl(double fuel_liters, double ratio) {
    if (fuel_liters <= 0.0 || ratio <= 0.0) return 0.0;
    return (fuel_liters * 1000.0) / ratio;
}

void SecondaryScreenRenderer::render(OledDisplay& display, SecondaryScreenMode mode, const TelemetrySnapshot& data) {
    display.clear();

    if (mode == SecondaryScreenMode::THERMAL_WARMUP) {
        // Oben: Kühlwasser-Fortschritt bis 70 °C Warm-Up-Schwelle
        double warmup_ratio = data.coolant_c / rx7::cfg::WARMUP_COOLANT_THRESHOLD_C;
        drawBar(display, 6, 8, 116, 10, warmup_ratio);

        std::ostringstream oss_w;
        oss_w << std::fixed << std::setprecision(1) << data.coolant_c;
        drawText(display, 6, 22, oss_w.str());

        std::ostringstream oss_o;
        oss_o << std::fixed << std::setprecision(1) << data.oil_c;
        drawText(display, 68, 22, oss_o.str());

        if (data.coolant_c < rx7::cfg::WARMUP_COOLANT_THRESHOLD_C) {
            // Pulsierender Warn-/Warm-Up-Balken unten
            drawBar(display, 6, 42, 116, 12, std::max(0.15, warmup_ratio));
        }
    } else if (mode == SecondaryScreenMode::PREMIX_CALC) {
        // Liter (oben) und benötigtes 2T-Öl in ml (unten) bei 1:200
        double ml = calcPremixMl(data.fuel_liters, rx7::cfg::PREMIX_RATIO);
        std::ostringstream oss_l;
        oss_l << std::fixed << std::setprecision(1) << data.fuel_liters;
        drawText(display, 10, 12, oss_l.str());

        std::ostringstream oss_ml;
        oss_ml << std::fixed << std::setprecision(0) << ml;
        drawText(display, 10, 34, oss_ml.str());
        drawBar(display, 10, 48, 108, 8, std::min(1.0, ml / 500.0));
    } else if (mode == SecondaryScreenMode::NIGHT_STEALTH) {
        // Minimalistische Nachtansicht: Uhrzeit + Höhe
        drawText(display, 10, 14, data.time_hm);
        std::ostringstream oss_alt;
        oss_alt << std::fixed << std::setprecision(0) << data.gps.altitude_m;
        drawText(display, 10, 34, oss_alt.str());
        if (data.gps.valid) {
            display.setPixel(118, 10, true);
            display.setPixel(119, 10, true);
        }
    }
}