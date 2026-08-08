#pragma once

#include <algorithm>
#include <cmath>

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
