#ifndef TURBOMAC_DRIVER_VALIDATION_H
#define TURBOMAC_DRIVER_VALIDATION_H

#include <stdbool.h>
#include <stdint.h>

#include "TurboMacProtocol.h"

static inline bool tm_validate_limit_request(
    const TurboMacLimitRequest *request,
    uint64_t last_sequence,
    uint32_t floor_mw,
    uint32_t ceiling_pl1_mw,
    uint32_t ceiling_pl2_mw,
    uint32_t watchdog_timeout_ms
) {
    return request != 0
        && request->version == TURBOMAC_PROTOCOL_VERSION
        && request->size == sizeof(*request)
        && request->watchdog_timeout_ms == watchdog_timeout_ms
        && (request->hwp_mode == kTurboMacHWPReleaseMaximum
            || request->hwp_mode == kTurboMacHWPBootstrapAndReleaseMaximum)
        && request->sequence > last_sequence
        && request->pl1_mw >= floor_mw
        && request->pl2_mw >= request->pl1_mw
        && ceiling_pl1_mw > 0U
        && ceiling_pl2_mw > 0U
        && request->pl1_mw <= ceiling_pl1_mw
        && request->pl2_mw <= ceiling_pl2_mw;
}

static inline bool tm_watchdog_expired(
    uint64_t elapsed_nanoseconds,
    uint32_t timeout_milliseconds
) {
    return elapsed_nanoseconds
        >= (uint64_t)timeout_milliseconds * UINT64_C(1000000);
}

#endif
