#include "Calibration.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    assert(turboMacCalibrationTierResponsive(5.0, 4.0));
    assert(turboMacCalibrationTierResponsive(20.0, 13.0));
    assert(!turboMacCalibrationTierResponsive(21.0, 13.0));
    assert(!turboMacCalibrationTierResponsive(20.0, 22.1));
    assert(!turboMacCalibrationTierResponsive(NAN, 13.0));
    assert(!turboMacCalibrationTierResponsive(20.0, NAN));
    assert(turboMacCalibrationBelowEffectiveFloor(5.0, 9.83));
    assert(turboMacCalibrationBelowEffectiveFloor(7.0, 9.1));
    assert(!turboMacCalibrationBelowEffectiveFloor(9.0, 9.83));
    assert(!turboMacCalibrationBelowEffectiveFloor(5.0, 4.0));
    assert(!turboMacCalibrationBelowEffectiveFloor(NAN, 9.83));
    const std::vector<TurboMacCalibrationPoint> mapping = {
        {29.0, 28.69, 38.12},
        {31.0, 30.60, 40.56},
        {33.0, 32.66, 42.83},
        {35.0, 34.57, 45.16},
    };
    assert(turboMacCalibrationBurstBase(mapping, 45.003, 2.0) == 33.0);
    assert(turboMacCalibrationBurstBase(mapping, 45.003, 3.0) == 31.0);
    assert(turboMacCalibrationBurstBase(mapping, 30.0, 2.0) == 0.0);
    assert(turboMacCalibrationBurstBase(mapping, NAN, 2.0) == 0.0);
    assert(turboMacCalibrationPL2Ceiling(37.0, 33.0, 2.0, 60.0) == 37.0);
    assert(turboMacCalibrationPL2Ceiling(37.0, 33.0, 6.0, 60.0) == 39.0);
    assert(turboMacCalibrationPL2Ceiling(37.0, 33.0, 6.0, 38.0) == 38.0);
    assert(turboMacCalibrationPL2Ceiling(37.0, 33.0, 6.0, 36.0) == 0.0);
    std::cout << "calibration response tests passed\n";
    return 0;
}
