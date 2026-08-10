#include "../Daemon/Policy.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

double advance(
    GovernorPolicy &policy,
    double start,
    double duration,
    double input,
    double capacity,
    double package
) {
    double nextSMC = start;
    const double end = start + duration;
    for (double now = start; now <= end + 1e-9; now += 0.1) {
        assert(policy.updatePackagePower(now, package));
        if (now + 1e-9 >= nextSMC) {
            assert(policy.updateSMC(now, input, capacity));
            nextSMC += 0.25;
        }
        policy.evaluate(now);
    }
    return end + 0.1;
}

void expectClose(double actual, double expected, double tolerance = 0.01) {
    assert(std::abs(actual - expected) <= tolerance);
}

PolicyConfig config() {
    PolicyConfig value;
    value.reserveW = 2.0;
    value.raplFloorW = 5.0;
    value.raplCeilingPL1W = 45.0;
    value.raplCeilingPL2W = 55.0;
    value.cruisePL2BurstW = 4.0;
    return value;
}

} // namespace

int main() {
    GovernorPolicy policy(config());
    double now = advance(policy, 0.0, 2.0, 20.0, 79.496, 5.0);
    PolicySnapshot snapshot = policy.snapshot();
    assert(snapshot.ready);
    expectClose(snapshot.guardW, 45.0026856);
    expectClose(snapshot.shedW, 51.9983336);
    expectClose(snapshot.emergencyW, 58.0002816);
    assert(snapshot.band == GovernorBand::Cruise);

    now = advance(policy, now, 3.2, 46.0, 79.496, 30.0);
    assert(policy.snapshot().band == GovernorBand::Guard);
    now = advance(policy, now, 2.2, 53.0, 79.496, 38.0);
    assert(policy.snapshot().band == GovernorBand::Shed);
    now = advance(policy, now, 0.2, 58.1, 79.496, 42.0);
    assert(policy.snapshot().band == GovernorBand::Emergency);

    now = advance(policy, now, 6.2, 40.0, 79.496, 25.0);
    assert(policy.snapshot().band == GovernorBand::Shed);
    now = advance(policy, now, 11.2, 40.0, 79.496, 25.0);
    assert(policy.snapshot().band == GovernorBand::Guard);
    now = advance(policy, now, 16.2, 40.0, 79.496, 25.0);
    assert(policy.snapshot().band == GovernorBand::Cruise);

    GovernorPolicy downscaled(config());
    advance(downscaled, 0.0, 2.0, 15.0, 60.0, 4.0);
    expectClose(downscaled.snapshot().guardW, 33.966);
    expectClose(downscaled.snapshot().shedW, 39.246);
    expectClose(downscaled.snapshot().emergencyW, 43.776);

    GovernorPolicy upscaled(config());
    advance(upscaled, 0.0, 2.0, 20.0, 100.0, 5.0);
    expectClose(upscaled.snapshot().guardW, 56.61);
    expectClose(upscaled.snapshot().shedW, 65.41);
    expectClose(upscaled.snapshot().emergencyW, 72.96);

    PolicyConfig mappedConfig = config();
    mappedConfig.packageToInputSlope = 1.5;
    mappedConfig.packageToInputInterceptW = 10.0;
    GovernorPolicy mapped(mappedConfig);
    advance(mapped, 0.0, 2.0, 20.0, 79.496, 33.0);
    assert(mapped.snapshot().predictedInputW > mapped.snapshot().emergencyW);
    assert(mapped.snapshot().band == GovernorBand::Emergency);

    GovernorPolicy tiers(config());
    double tierTime = advance(tiers, 0.0, 2.0, 20.0, 79.496, 5.0);
    tierTime = advance(tiers, tierTime, 0.2, 45.0, 79.496, 30.0);
    assert(tiers.snapshot().band != GovernorBand::Emergency);
    tierTime = advance(tiers, tierTime, 2.2, 52.0, 79.496, 37.0);
    assert(tiers.snapshot().band == GovernorBand::Shed);
    advance(tiers, tierTime, 0.2, 58.1, 79.496, 42.0);
    assert(tiers.snapshot().band == GovernorBand::Emergency);
    expectClose(tiers.snapshot().pl1W, 5.0);
    expectClose(tiers.snapshot().pl2W, 5.0);

    GovernorPolicy limiter(config());
    double limitTime = advance(limiter, 0.0, 2.0, 40.0, 79.496, 5.0);
    const double lowered = limiter.snapshot().pl1W;
    limitTime = advance(limiter, limitTime, 4.8, 20.0, 79.496, 5.0);
    assert(limiter.snapshot().pl1W >= lowered + 3.0);
    const double afterFirstWindow = limiter.snapshot().pl1W;
    advance(limiter, limitTime, 0.4, 20.0, 79.496, 5.0);
    assert(limiter.snapshot().pl1W <= afterFirstWindow + 1.01);

    PolicyConfig skewConfig = config();
    skewConfig.reserveW = 9.5;
    skewConfig.raplFloorW = 9.0;
    skewConfig.raplCeilingPL1W = 37.0;
    skewConfig.raplCeilingPL2W = 37.0;
    skewConfig.cruisePL2BurstW = 10.0;
    skewConfig.packageToInputSlope = 1.205789;
    skewConfig.packageToInputInterceptW = 3.459693;
    GovernorPolicy phaseSkew(skewConfig);
    double skewTime = advance(phaseSkew, 0.0, 2.0, 13.0, 79.496, 6.0);
    assert(phaseSkew.snapshot().band == GovernorBand::Cruise);

    // Reproduce the captured phase error: PDTR rises while the instantaneous
    // package sample is still low, then package power catches up before the
    // next SMC sample. Re-evaluating the stale pair must not amplify non-CPU
    // power or manufacture an emergency transition.
    assert(phaseSkew.updateSMC(skewTime, 42.646, 79.496));
    phaseSkew.evaluate(skewTime);
    const double oncePerSMC = phaseSkew.snapshot().nonCPUW;
    for (unsigned repeat = 1U; repeat <= 4U; ++repeat) {
        phaseSkew.evaluate(skewTime + 0.05 * (double)repeat);
        expectClose(phaseSkew.snapshot().nonCPUW, oncePerSMC, 0.0001);
    }
    skewTime += 0.21;
    assert(phaseSkew.updatePackagePower(skewTime, 29.661));
    const PolicySnapshot skewed = phaseSkew.evaluate(skewTime);
    assert(skewed.band == GovernorBand::Cruise);
    assert(skewed.predictedInputW < skewed.emergencyW);
    assert(skewed.nonCPUW < 15.0);
    assert(skewed.filteredPackageW < skewed.packageW);

    GovernorPolicy recovery(config());
    advance(recovery, 0.0, 2.0, 20.0, 79.496, 10.0);
    expectClose(recovery.snapshot().recoveryIntervalS, 1.0);
    GovernorPolicy nearGuard(config());
    advance(nearGuard, 0.0, 2.0, 44.0, 79.496, 34.0);
    expectClose(nearGuard.snapshot().recoveryIntervalS, 5.0);

    std::cout << "policy scaling, coherent rails, 45/52/58 tiers, hysteresis, and adaptive recovery tests passed\n";
    return 0;
}
