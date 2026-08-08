#include "../Daemon/Energy.h"
#include "../Shared/DriverValidation.h"
#include "../Shared/RAPLCodec.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>

int main() {
    const uint32_t exponent = 3U;
    assert(tm_rapl_mw_to_raw(45000U, exponent) == 360U);
    assert(tm_rapl_raw_to_mw(360U, exponent) == 45000U);
    assert(tm_rapl_mw_to_raw(1U, exponent) == 1U);

    const uint64_t timeAndReserved = UINT64_C(0x1234567812345678) | TM_RAPL_LOCK;
    const uint64_t encoded = tm_rapl_with_power_limits(timeAndReserved, 360U, 416U);
    assert((encoded & TM_RAPL_LOCK) == 0U);
    assert((encoded & TM_RAPL_PL1_ENABLE) != 0U);
    assert((encoded & TM_RAPL_PL1_CLAMP) != 0U);
    assert((encoded & TM_RAPL_PL2_ENABLE) != 0U);
    assert((encoded & TM_RAPL_PL2_CLAMP) != 0U);
    assert(tm_rapl_pl1_raw(encoded) == 360U);
    assert(tm_rapl_pl2_raw(encoded) == 416U);

    double watts = 0.0;
    assert(turboMacPackagePowerWatts(
        UINT32_C(0xfffffff0), UINT32_C(0x10), 0U, 2.0, &watts
    ));
    assert(std::abs(watts - 16.0) < 1e-9);
    assert(!turboMacPackagePowerWatts(1U, 2U, 32U, 0.1, &watts));
    assert(!turboMacPackagePowerWatts(1U, 2U, 14U, 2.1, &watts));

    TurboMacEnergyTracker tracker;
    assert(tracker.sample(100U, 14U, 1.0, &watts)
        == TurboMacEnergySampleResult::Baseline);
    assert(tracker.sample(200U, 14U, 4.0, &watts)
        == TurboMacEnergySampleResult::Invalid);
    assert(tracker.sample(16584U, 14U, 5.0, &watts)
        == TurboMacEnergySampleResult::Valid);
    assert(std::abs(watts - 1.0) < 1e-9);
    tracker.reset();
    assert(tracker.sample(UINT32_C(0xfffffff0), 0U, 10.0, &watts)
        == TurboMacEnergySampleResult::Baseline);
    assert(tracker.sample(UINT32_C(0x10), 0U, 12.0, &watts)
        == TurboMacEnergySampleResult::Valid);
    assert(std::abs(watts - 16.0) < 1e-9);

    TurboMacLimitRequest request = {};
    request.version = TURBOMAC_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.pl1_mw = 20000U;
    request.pl2_mw = 25000U;
    request.watchdog_timeout_ms = 5000U;
    request.hwp_mode = kTurboMacHWPReleaseMaximum;
    request.sequence = 11U;
    assert(tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));
    request.version++;
    assert(!tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));
    request.version = TURBOMAC_PROTOCOL_VERSION;
    request.sequence = 10U;
    assert(!tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));
    request.sequence = 12U;
    request.pl2_mw = 19000U;
    assert(!tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));
    request.pl2_mw = 25000U;
    request.watchdog_timeout_ms = 4999U;
    assert(!tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));
    request.watchdog_timeout_ms = 5000U;
    request.hwp_mode = kTurboMacHWPModeInvalid;
    assert(!tm_validate_limit_request(
        &request, 10U, 5000U, 45000U, 55000U, 5000U
    ));

    assert(!tm_watchdog_expired(UINT64_C(4999999999), 5000U));
    assert(tm_watchdog_expired(UINT64_C(5000000000), 5000U));

    std::cout << "RAPL codec, wraparound, request, and watchdog tests passed\n";
    return 0;
}
