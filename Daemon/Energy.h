#ifndef TURBOMAC_ENERGY_H
#define TURBOMAC_ENERGY_H

#include <cmath>
#include <cstdint>

inline bool turboMacPackagePowerWatts(
    uint32_t previousRaw,
    uint32_t currentRaw,
    uint32_t energyExponent,
    double elapsedSeconds,
    double *watts
) {
    if (watts == nullptr
        || energyExponent >= 32U
        || !std::isfinite(elapsedSeconds)
        || elapsedSeconds <= 0.0
        || elapsedSeconds > 2.0) {
        return false;
    }
    const uint32_t delta = currentRaw - previousRaw;
    const double joules = std::ldexp((double)delta, -(int)energyExponent);
    const double result = joules / elapsedSeconds;
    if (!std::isfinite(result) || result < 0.0 || result > 500.0) {
        return false;
    }
    *watts = result;
    return true;
}

#endif
