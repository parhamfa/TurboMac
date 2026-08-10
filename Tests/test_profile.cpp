#include "../Daemon/Profile.h"

#include <cassert>
#include <iostream>
#include <string>

namespace {

CalibrationProfile validProfile() {
    CalibrationProfile profile;
    profile.schemaVersion = 2U;
    profile.identity.hardwareUUID = "test-hardware";
    profile.identity.modelIdentifier = "MacBookPro14,3";
    profile.identity.cpuBrand = "Intel test CPU";
    profile.identity.osProductVersion = "13.7.8";
    profile.identity.osBuild = "22H730";
    profile.identity.pdtrType = "flt ";
    profile.identity.acpwType = "ui32";
    profile.calibratedCapacityW = 79.496;
    profile.reserveW = 2.0;
    profile.raplFloorW = 9.0;
    profile.raplCeilingPL1W = 43.0;
    profile.raplCeilingPL2W = 45.0;
    profile.cruisePL2BurstW = 2.0;
    profile.packageToInputSlope = 1.205789;
    profile.packageToInputInterceptW = 3.459693;
    profile.guardRatio = 55.0 / 79.496;
    profile.shedRatio = 60.0 / 79.496;
    profile.emergencyRatio = 65.0 / 79.496;
    profile.cruiseTargetRatio = 55.0 / 79.496;
    profile.guardTargetRatio = 48.0 / 79.496;
    profile.shedTargetRatio = 42.0 / 79.496;
    profile.calibrationPointCount = 15U;
    profile.validationPassed = true;
    profile.autoArm = true;
    profile.calibratedAt = 100;
    profile.validatedAt = 200;
    return profile;
}

} // namespace

int main() {
    std::string error;
    const CalibrationProfile valid = validProfile();
    assert(ProfileStore::validate(valid, &error));

    CalibrationProfile legacy = valid;
    legacy.schemaVersion = 1U;
    assert(ProfileStore::validate(legacy, &error));

    CalibrationProfile unorderedBands = valid;
    unorderedBands.shedRatio = unorderedBands.guardRatio;
    assert(!ProfileStore::validate(unorderedBands, &error));

    CalibrationProfile weakEmergencyReserve = valid;
    weakEmergencyReserve.emergencyRatio = 0.96;
    assert(!ProfileStore::validate(weakEmergencyReserve, &error));

    CalibrationProfile unorderedTargets = valid;
    unorderedTargets.guardTargetRatio = unorderedTargets.cruiseTargetRatio;
    assert(!ProfileStore::validate(unorderedTargets, &error));

    CalibrationProfile targetAboveGuard = valid;
    targetAboveGuard.cruiseTargetRatio = targetAboveGuard.guardRatio + 0.01;
    assert(!ProfileStore::validate(targetAboveGuard, &error));

    CalibrationProfile unsupportedSchema = valid;
    unsupportedSchema.schemaVersion = 3U;
    assert(!ProfileStore::validate(unsupportedSchema, &error));

    std::cout << "profile schema and independent control-policy bounds tests passed\n";
    return 0;
}
