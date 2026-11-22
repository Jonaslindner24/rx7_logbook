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
    GpsReader() = default;
    ~GpsReader();

    bool openPort(const std::string &device_path, int baud_rate = 9600);
    void closePort();
    GpsFix poll(const std::string &device_path);

    // Direkt testbar ohne Hardware
    bool feedNmeaSentence(const std::string &sentence, GpsFix &state);
    static double nmeaCoordToDecimal(const std::string &dm_str, char hemi);
    static double haversineKm(double lat1, double lon1, double lat2, double lon2);

private:
    int fd_{-1};
    std::string rx_buffer_;
    GpsFix last_fix_{};
};