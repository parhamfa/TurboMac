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

enum class TurboMacEnergySampleResult {
    Baseline,
    Valid,
    Invalid,
};

class TurboMacEnergyTracker {
public:
    TurboMacEnergyTracker() {
        reset();
    }

    void reset() {
        initialized_ = false;
        previousRaw_ = 0U;
        previousTime_ = -1.0;
    }

    TurboMacEnergySampleResult sample(
        uint32_t currentRaw,
        uint32_t energyExponent,
        double nowSeconds,
        double *watts
    ) {
        if (watts == nullptr || !std::isfinite(nowSeconds) || nowSeconds < 0.0) {
            return TurboMacEnergySampleResult::Invalid;
        }
        if (!initialized_) {
            initialized_ = true;
            previousRaw_ = currentRaw;
            previousTime_ = nowSeconds;
            *watts = 0.0;
            return TurboMacEnergySampleResult::Baseline;
        }

        const uint32_t priorRaw = previousRaw_;
        const double elapsed = nowSeconds - previousTime_;
        previousRaw_ = currentRaw;
        previousTime_ = nowSeconds;
        return turboMacPackagePowerWatts(
            priorRaw,
            currentRaw,
            energyExponent,
            elapsed,
            watts
        ) ? TurboMacEnergySampleResult::Valid
          : TurboMacEnergySampleResult::Invalid;
    }

    uint32_t previousRaw() const {
        return previousRaw_;
    }

    double previousTime() const {
        return previousTime_;
    }

private:
    bool initialized_;
    uint32_t previousRaw_;
    double previousTime_;
};

#endif
