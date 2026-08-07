#include "../Daemon/TemperatureReader.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    double temperature = 0.0;
    std::string error;
    const std::string sample =
        "**** SMC sensors ****\nCPU Thermal level: 0\n"
        "CPU die temperature: 63.37 C\nGPU die temperature: 53.56 C\n";
    assert(TemperatureReader::parsePowermetrics(sample, &temperature, &error));
    assert(std::abs(temperature - 63.37) < 0.001);
    assert(!TemperatureReader::parsePowermetrics("CPU die temperature: nan C", &temperature, &error));
    assert(!TemperatureReader::parsePowermetrics("CPU Thermal level: 0", &temperature, &error));
    if (argc == 2 && std::string(argv[1]) == "--live") {
        assert(TemperatureReader::readCPUDie(&temperature, &error));
        std::cout << "live CPU die temperature: " << temperature << " C\n";
    }
    std::cout << "powermetrics CPU temperature parser tests passed\n";
    return 0;
}
