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
    std::cout << "calibration response tests passed\n";
    return 0;
}
