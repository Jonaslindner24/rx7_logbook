#include "hardware/gps_reader.hpp"
#include <cmath>

namespace
{
    constexpr double EARTH_RADIUS_KM = 6371.0;
    constexpr double DEG2RAD = M_PI / 180.0;
} // namespace

GpsFix GpsReader::poll(const std::string &device_path)
{
    (void)device_path;
#ifdef MOCK_HARDWARE
    // Simulierte Position im Raum Steiermark (Raabklamm / Weiz)
    return GpsFix{true, 47.2240, 15.5880, 54.0, 495.0};
#else
    // TODO: Seriellen Port öffnen (termios, 9600/38400 Baud) und $GPRMC / $GNRMC / $GPGGA parsen
    return GpsFix{false, 0.0, 0.0, 0.0, 0.0};
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