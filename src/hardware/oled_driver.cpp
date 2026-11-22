#include "hardware/oled_driver.hpp"
#include "config.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#ifndef MOCK_HARDWARE
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

OledDisplay::OledDisplay(uint8_t address, const std::string &name)
    : addr_(address), name_(name)
{
    buffer_.fill(0);
}

OledDisplay::~OledDisplay()
{
#ifndef MOCK_HARDWARE
    if (i2c_fd_ >= 0)
    {
        close(i2c_fd_);
    }
#endif
}

void OledDisplay::sendCommand(uint8_t cmd)
{
#ifndef MOCK_HARDWARE
    if (i2c_fd_ < 0)
        return;
    uint8_t payload[2] = {0x00, cmd};
    (void)write(i2c_fd_, payload, sizeof(payload));
#else
    (void)cmd;
#endif
}

bool OledDisplay::init()
{
#ifndef MOCK_HARDWARE
    i2c_fd_ = open(rx7::cfg::I2C_BUS_PATH, O_RDWR);
    if (i2c_fd_ < 0)
    {
        std::cerr << "[" << name_ << "] Konnte I2C-Bus nicht oeffnen\n";
        return false;
    }
    if (ioctl(i2c_fd_, I2C_SLAVE, addr_) < 0)
    {
        std::cerr << "[" << name_ << "] I2C-Adresse 0x" << std::hex << (int)addr_ << " nicht erreichbar\n";
        return false;
    }

    const uint8_t init_sequence[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x30, 0xA4, 0xA6, 0xAF};
    for (uint8_t c : init_sequence)
    {
        sendCommand(c);
    }
#endif
    clear();
    return true;
}

void OledDisplay::clear()
{
    buffer_.fill(0);
}

void OledDisplay::setPixel(int x, int y, bool on)
{
    if (x < 0 || x >= rx7::cfg::OLED_WIDTH || y < 0 || y >= rx7::cfg::OLED_HEIGHT)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(y / 8) * rx7::cfg::OLED_WIDTH + static_cast<size_t>(x);
    const uint8_t mask = static_cast<uint8_t>(1U << (y % 8));
    if (on)
    {
        buffer_[idx] |= mask;
    }
    else
    {
        buffer_[idx] &= static_cast<uint8_t>(~mask);
    }
}

void OledDisplay::drawCircle(int cx, int cy, int r, bool on)
{
    for (int deg = 0; deg < 360; deg += 5)
    {
        double rad = deg * (M_PI / 180.0);
        int x = cx + static_cast<int>(std::lround(r * std::cos(rad)));
        int y = cy + static_cast<int>(std::lround(r * std::sin(rad)));
        setPixel(x, y, on);
    }
}

void OledDisplay::present()
{
#ifndef MOCK_HARDWARE
    if (i2c_fd_ < 0)
        return;
    sendCommand(0x21);
    sendCommand(0);
    sendCommand(127);
    sendCommand(0x22);
    sendCommand(0);
    sendCommand(7);

    uint8_t packet[1 + rx7::cfg::OLED_WIDTH];
    packet[0] = 0x40;
    for (int page = 0; page < 8; ++page)
    {
        std::memcpy(&packet[1], &buffer_[static_cast<size_t>(page) * rx7::cfg::OLED_WIDTH], rx7::cfg::OLED_WIDTH);
        (void)write(i2c_fd_, packet, sizeof(packet));
    }
#else
    int active_pixels = static_cast<int>(std::count_if(buffer_.begin(), buffer_.end(), [](uint8_t b)
                                                       { return b != 0; }));
    std::cout << "[MOCK " << name_ << " @ 0x" << std::hex << (int)addr_ << std::dec
              << "] Frame gerendert (Bytes!=0: " << active_pixels << ")\n";
#endif
}