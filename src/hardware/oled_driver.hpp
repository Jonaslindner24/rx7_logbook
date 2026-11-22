#pragma once
#include <array>
#include <cstdint>
#include <string>

class OledDisplay
{
public:
    OledDisplay(uint8_t address, const std::string &name);
    ~OledDisplay();

    bool init();
    void clear();
    void setPixel(int x, int y, bool on);
    void drawCircle(int cx, int cy, int r, bool on);
    void present();

private:
    uint8_t addr_;
    std::string name_;
    int i2c_fd_{-1};
    std::array<uint8_t, 128 * 64 / 8> buffer_{};

    void sendCommand(uint8_t cmd);
};