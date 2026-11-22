#include "hardware/gps_reader.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#ifndef MOCK_HARDWARE
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{

    constexpr double EARTH_RADIUS_KM = 6371.0;
    constexpr double DEG2RAD = M_PI / 180.0;
    constexpr double KNOTS_TO_KMH = 1.852;

    std::vector<std::string> splitCsv(const std::string &s)
    {
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            out.push_back(item);
        }
        return out;
    }

    bool verifyChecksum(const std::string &raw)
    {
        auto dollar = raw.find('$');
        auto star = raw.find('*');
        if (dollar == std::string::npos || star == std::string::npos || star + 2 >= raw.size())
        {
            return true; // Falls kein Checksummen-Suffix vorliegt
        }
        uint8_t cs = 0;
        for (size_t i = dollar + 1; i < star; ++i)
        {
            cs ^= static_cast<uint8_t>(raw[i]);
        }
        std::string hex_str = raw.substr(star + 1, 2);
        unsigned int expected = std::strtoul(hex_str.c_str(), nullptr, 16);
        return cs == static_cast<uint8_t>(expected);
    }

} // namespace

GpsReader::~GpsReader()
{
    closePort();
}

bool GpsReader::openPort(const std::string &device_path, int baud_rate)
{
#ifndef MOCK_HARDWARE
    if (fd_ >= 0)
        return true;
    fd_ = open(device_path.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
        return false;

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0)
    {
        closePort();
        return false;
    }

    speed_t speed = B9600;
    if (baud_rate == 38400)
        speed = B38400;
    else if (baud_rate == 115200)
        speed = B115200;

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT | ICRNL | INLCR);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd_, TCSANOW, &tty);
#else
    (void)device_path;
    (void)baud_rate;
#endif
    return true;
}

void GpsReader::closePort()
{
#ifndef MOCK_HARDWARE
    if (fd_ >= 0)
    {
        close(fd_);
        fd_ = -1;
    }
#endif
}

double GpsReader::nmeaCoordToDecimal(const std::string &dm_str, char hemi)
{
    if (dm_str.empty())
        return 0.0;
    double val = std::atof(dm_str.c_str());
    int deg = static_cast<int>(val / 100.0);
    double minutes = val - (deg * 100.0);
    double deg_dec = deg + (minutes / 60.0);
    if (hemi == 'S' || hemi == 'W')
    {
        deg_dec = -deg_dec;
    }
    return deg_dec;
}

bool GpsReader::feedNmeaSentence(const std::string &sentence, GpsFix &state)
{
    if (sentence.empty() || sentence.front() != '$')
        return false;
    if (!verifyChecksum(sentence))
        return false;

    std::string clean = sentence;
    auto star = clean.find('*');
    if (star != std::string::npos)
    {
        clean = clean.substr(0, star);
    }
    while (!clean.empty() && (clean.back() == '\r' || clean.back() == '\n'))
    {
        clean.pop_back();
    }

    auto fields = splitCsv(clean);
    if (fields.empty())
        return false;

    const std::string &talker_type = fields[0];
    bool is_rmc = (talker_type.size() >= 6 && talker_type.substr(3) == "RMC");
    bool is_gga = (talker_type.size() >= 6 && talker_type.substr(3) == "GGA");

    if (is_rmc && fields.size() > 7)
    {
        // $GNRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,knots,...
        state.valid = (fields[2] == "A");
        if (state.valid && !fields[3].empty() && !fields[4].empty() && !fields[5].empty() && !fields[6].empty())
        {
            state.lat = nmeaCoordToDecimal(fields[3], fields[4][0]);
            state.lon = nmeaCoordToDecimal(fields[5], fields[6][0]);
        }
        if (!fields[7].empty())
        {
            state.speed_kmh = std::atof(fields[7].c_str()) * KNOTS_TO_KMH;
        }
        return true;
    }

    if (is_gga && fields.size() > 9)
    {
        // $GNGGA,hhmmss.ss,lat,NS,lon,EW,fix_quality,sats,hdop,alt,M,...
        int fix_q = fields[6].empty() ? 0 : std::atoi(fields[6].c_str());
        if (fix_q > 0)
        {
            state.valid = true;
            if (!fields[2].empty() && !fields[3].empty() && !fields[4].empty() && !fields[5].empty())
            {
                state.lat = nmeaCoordToDecimal(fields[2], fields[3][0]);
                state.lon = nmeaCoordToDecimal(fields[4], fields[5][0]);
            }
        }
        if (!fields[9].empty())
        {
            state.altitude_m = std::atof(fields[9].c_str());
        }
        return true;
    }

    return false;
}

GpsFix GpsReader::poll(const std::string &device_path)
{
#ifdef MOCK_HARDWARE
    (void)device_path;
    return GpsFix{true, 47.2240, 15.5880, 54.0, 495.0};
#else
    if (fd_ < 0)
    {
        openPort(device_path, 9600);
    }
    if (fd_ >= 0)
    {
        char buf[256];
        ssize_t n = read(fd_, buf, sizeof(buf));
        if (n > 0)
        {
            rx_buffer_.append(buf, static_cast<size_t>(n));
            size_t pos = 0;
            while ((pos = rx_buffer_.find('\n')) != std::string::npos)
            {
                std::string line = rx_buffer_.substr(0, pos + 1);
                rx_buffer_.erase(0, pos + 1);
                feedNmeaSentence(line, last_fix_);
            }
        }
    }
    return last_fix_;
#endif
}

double GpsReader::haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    double d_lat = (lat2 - lat1) * DEG2RAD;
    double d_lon = (lon2 - lon1) * DEG2RAD;
    double a = std::sin(d_lat / 2.0) * std::sin(d_lat / 2.0) +
               std::cos(lat1 * DEG2RAD) * std::cos(lat2 * DEG2RAD) *
                   std::sin(d_lon / 2.0) * std::sin(d_lon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return EARTH_RADIUS_KM * c;
}