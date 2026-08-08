#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

struct TurboMacCalibrationPoint {
    double requestedLimitW;
    double averagePackageW;
    double averageInputW;
};

inline bool turboMacCalibrationTierResponsive(
    double requestedLimitW,
    double averagePackageW
) {
    return std::isfinite(requestedLimitW)
        && std::isfinite(averagePackageW)
        && requestedLimitW > 0.0
        && averagePackageW >= std::max(1.0, requestedLimitW * 0.65)
        && averagePackageW <= requestedLimitW + 2.0;
}

inline bool turboMacCalibrationBelowEffectiveFloor(
    double requestedLimitW,
    double averagePackageW
) {
    return std::isfinite(requestedLimitW)
        && std::isfinite(averagePackageW)
        && requestedLimitW > 0.0
        && averagePackageW > requestedLimitW + 2.0;
}

inline double turboMacCalibrationBurstBase(
    const std::vector<TurboMacCalibrationPoint> &mapping,
    double guardW,
    double reserveW
) {
    if (!std::isfinite(guardW) || !std::isfinite(reserveW)
        || guardW <= 0.0 || reserveW < 0.0) {
        return 0.0;
    }
    double selected = 0.0;
    for (const TurboMacCalibrationPoint &point : mapping) {
        if (std::isfinite(point.requestedLimitW)
            && std::isfinite(point.averageInputW)
            && point.requestedLimitW > 0.0
            && point.averageInputW + reserveW <= guardW) {
            selected = std::max(selected, point.requestedLimitW);
        }
    }
    return selected;
}

inline double turboMacCalibrationPL2Ceiling(
    double pl1CeilingW,
    double burstBaseW,
    double selectedBurstW,
    double capturedPL2W
) {
    if (!std::isfinite(pl1CeilingW) || !std::isfinite(burstBaseW)
        || !std::isfinite(selectedBurstW) || !std::isfinite(capturedPL2W)
        || pl1CeilingW <= 0.0 || burstBaseW < 0.0
        || selectedBurstW < 0.0 || capturedPL2W < pl1CeilingW) {
        return 0.0;
    }
    const double testedAbsolutePL2 = burstBaseW + selectedBurstW;
    return std::min(
        capturedPL2W,
        std::max(pl1CeilingW, testedAbsolutePL2)
    );
}
