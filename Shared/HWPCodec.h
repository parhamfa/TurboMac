#ifndef TURBOMAC_HWP_CODEC_H
#define TURBOMAC_HWP_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#define TM_HWP_PERFORMANCE_MASK UINT64_C(0xff)
#define TM_HWP_MAXIMUM_SHIFT 8U
#define TM_HWP_DESIRED_SHIFT 16U
#define TM_HWP_EPP_SHIFT 24U
#define TM_HWP_ACTIVITY_WINDOW_SHIFT 32U
#define TM_HWP_PACKAGE_CONTROL (UINT64_C(1) << 42U)
#define TM_HWP_ACTIVITY_WINDOW_VALID (UINT64_C(1) << 59U)
#define TM_HWP_EPP_VALID (UINT64_C(1) << 60U)
#define TM_HWP_DESIRED_VALID (UINT64_C(1) << 61U)
#define TM_HWP_MAXIMUM_VALID (UINT64_C(1) << 62U)
#define TM_HWP_MINIMUM_VALID (UINT64_C(1) << 63U)

static inline uint32_t tm_hwp_highest(uint64_t capabilities) {
    return (uint32_t)(capabilities & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_guaranteed(uint64_t capabilities) {
    return (uint32_t)((capabilities >> 8U) & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_most_efficient(uint64_t capabilities) {
    return (uint32_t)((capabilities >> 16U) & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_lowest(uint64_t capabilities) {
    return (uint32_t)((capabilities >> 24U) & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_minimum(uint64_t request) {
    return (uint32_t)(request & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_maximum(uint64_t request) {
    return (uint32_t)((request >> TM_HWP_MAXIMUM_SHIFT) & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_desired(uint64_t request) {
    return (uint32_t)((request >> TM_HWP_DESIRED_SHIFT) & TM_HWP_PERFORMANCE_MASK);
}

static inline uint32_t tm_hwp_epp(uint64_t request) {
    return (uint32_t)((request >> TM_HWP_EPP_SHIFT) & TM_HWP_PERFORMANCE_MASK);
}

static inline bool tm_hwp_package_control(uint64_t request) {
    return (request & TM_HWP_PACKAGE_CONTROL) != 0U;
}

static inline bool tm_hwp_maximum_valid(uint64_t request) {
    return (request & TM_HWP_MAXIMUM_VALID) != 0U;
}

static inline bool tm_hwp_capabilities_sane(uint64_t capabilities) {
    const uint32_t lowest = tm_hwp_lowest(capabilities);
    const uint32_t efficient = tm_hwp_most_efficient(capabilities);
    const uint32_t guaranteed = tm_hwp_guaranteed(capabilities);
    const uint32_t highest = tm_hwp_highest(capabilities);
    return highest > 0U
        && lowest <= highest
        && efficient <= highest
        && guaranteed <= highest;
}

static inline uint32_t tm_hwp_effective_maximum(
    uint64_t logical_request,
    uint64_t package_request,
    bool flexible_request_fields
) {
    if (!tm_hwp_package_control(logical_request)
        || (flexible_request_fields && tm_hwp_maximum_valid(logical_request))) {
        return tm_hwp_maximum(logical_request);
    }
    return tm_hwp_maximum(package_request);
}

static inline uint64_t tm_hwp_with_maximum(uint64_t request, uint32_t maximum) {
    const uint64_t maximum_mask = TM_HWP_PERFORMANCE_MASK << TM_HWP_MAXIMUM_SHIFT;
    return (request & ~maximum_mask)
        | (((uint64_t)maximum & TM_HWP_PERFORMANCE_MASK) << TM_HWP_MAXIMUM_SHIFT);
}

static inline uint64_t tm_hwp_with_local_maximum_override(
    uint64_t request,
    uint32_t maximum,
    bool flexible_request_fields
) {
    uint64_t result = tm_hwp_with_maximum(request, maximum);
    if (tm_hwp_package_control(request) && flexible_request_fields) {
        result |= TM_HWP_MAXIMUM_VALID;
    }
    return result;
}

#endif
