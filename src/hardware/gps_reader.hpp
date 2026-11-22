#pragma once
#include <string>

struct GpsFix
{
    bool valid{false};
    double lat{0.0};
    double lon{0.0};
    double speed_kmh{0.0};
    double altitude_m{0.0};
};

class GpsReader
{
public:
    GpsFix poll(const std::string &device_path);
    static double haversineKm(double lat1, double lon1, double lat2, double lon2);
};