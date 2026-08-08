#include "Calibration.h"

#include <cassert>
#include <cmath>
#include <iostream>

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
    std::cout << "calibration response tests passed\n";
    return 0;
}
