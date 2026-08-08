#include "../Shared/HWPCodec.h"
#include "../Shared/TurboMacProtocol.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    static_assert(sizeof(TurboMacCapabilities) == 112U);
    static_assert(sizeof(TurboMacLimitRequest) == 32U);
    static_assert(sizeof(TurboMacHWPStatus) == 1592U);
    const uint64_t capabilities = UINT64_C(0x2080c0ff);
    assert(tm_hwp_highest(capabilities) == 0xffU);
    assert(tm_hwp_guaranteed(capabilities) == 0xc0U);
    assert(tm_hwp_most_efficient(capabilities) == 0x80U);
    assert(tm_hwp_lowest(capabilities) == 0x20U);
    assert(tm_hwp_capabilities_sane(capabilities));
    assert(tm_hwp_capabilities_sane(UINT64_C(0x000000ff)));
    assert(!tm_hwp_capabilities_sane(UINT64_C(0x2080c000)));
    assert(!tm_hwp_capabilities_sane(UINT64_C(0x20f0c080)));

    const uint64_t localRequest = UINT64_C(0x30)
        | (UINT64_C(0x70) << TM_HWP_MAXIMUM_SHIFT)
        | (UINT64_C(0x80) << TM_HWP_EPP_SHIFT)
        | (UINT64_C(0x155) << TM_HWP_ACTIVITY_WINDOW_SHIFT);
    assert(tm_hwp_minimum(localRequest) == 0x30U);
    assert(tm_hwp_maximum(localRequest) == 0x70U);
    assert(tm_hwp_desired(localRequest) == 0U);
    assert(tm_hwp_epp(localRequest) == 0x80U);
    assert(!tm_hwp_package_control(localRequest));
    assert(tm_hwp_effective_maximum(localRequest, 0U, false) == 0x70U);

    const uint64_t released = tm_hwp_with_local_maximum_override(
        localRequest, 0xffU, true
    );
    assert(tm_hwp_maximum(released) == 0xffU);
    assert((released & ~(TM_HWP_PERFORMANCE_MASK << TM_HWP_MAXIMUM_SHIFT))
        == (localRequest & ~(TM_HWP_PERFORMANCE_MASK << TM_HWP_MAXIMUM_SHIFT)));

    const uint64_t packageRequest = tm_hwp_with_maximum(
        UINT64_C(0x40990000000020), 0x90U
    );
    const uint64_t packageControlled = localRequest | TM_HWP_PACKAGE_CONTROL;
    assert(tm_hwp_package_control(packageControlled));
    assert(tm_hwp_effective_maximum(
        packageControlled, packageRequest, false
    ) == 0x90U);
    assert(tm_hwp_effective_maximum(
        packageControlled, packageRequest, true
    ) == 0x90U);

    const uint64_t flexibleOverride = tm_hwp_with_local_maximum_override(
        packageControlled, 0xffU, true
    );
    assert(tm_hwp_maximum_valid(flexibleOverride));
    assert(tm_hwp_effective_maximum(
        flexibleOverride, packageRequest, true
    ) == 0xffU);
    const uint64_t permittedChange =
        (TM_HWP_PERFORMANCE_MASK << TM_HWP_MAXIMUM_SHIFT) | TM_HWP_MAXIMUM_VALID;
    assert(((flexibleOverride ^ packageControlled) & ~permittedChange) == 0U);

    std::cout << "HWP capability, field preservation, and source-selection tests passed\n";
    return 0;
}
