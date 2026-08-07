#ifndef TURBOMAC_TEMPERATURE_READER_H
#define TURBOMAC_TEMPERATURE_READER_H

#include <string>

class TemperatureReader {
public:
    static bool parsePowermetrics(
        const std::string &output,
        double *temperatureC,
        std::string *error
    );
    static bool readCPUDie(double *temperatureC, std::string *error);
};

#endif
