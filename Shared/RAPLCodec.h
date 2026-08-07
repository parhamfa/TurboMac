#ifndef TURBOMAC_RAPL_CODEC_H
#define TURBOMAC_RAPL_CODEC_H

#include <stdint.h>

#define TM_RAPL_POWER_FIELD_MASK UINT64_C(0x7fff)
#define TM_RAPL_PL1_ENABLE (UINT64_C(1) << 15)
#define TM_RAPL_PL1_CLAMP (UINT64_C(1) << 16)
#define TM_RAPL_PL2_SHIFT 32
#define TM_RAPL_PL2_ENABLE (UINT64_C(1) << 47)
#define TM_RAPL_PL2_CLAMP (UINT64_C(1) << 48)
#define TM_RAPL_LOCK (UINT64_C(1) << 63)

static inline uint32_t tm_rapl_raw_to_mw(uint32_t raw, uint32_t exponent) {
    if (exponent >= 31U) {
        return 0;
    }
    const uint64_t divisor = UINT64_C(1) << exponent;
    return (uint32_t)(((uint64_t)raw * UINT64_C(1000) + divisor / 2U) / divisor);
}

static inline uint32_t tm_rapl_mw_to_raw(uint32_t mw, uint32_t exponent) {
    if (exponent >= 31U) {
        return 0;
    }
    const uint64_t scaled = (uint64_t)mw * (UINT64_C(1) << exponent);
    uint64_t raw = (scaled + UINT64_C(500)) / UINT64_C(1000);
    if (raw == 0U && mw > 0U) {
        raw = 1U;
    }
    if (raw > TM_RAPL_POWER_FIELD_MASK) {
        raw = TM_RAPL_POWER_FIELD_MASK;
    }
    return (uint32_t)raw;
}

static inline uint32_t tm_rapl_pl1_raw(uint64_t value) {
    return (uint32_t)(value & TM_RAPL_POWER_FIELD_MASK);
}

static inline uint32_t tm_rapl_pl2_raw(uint64_t value) {
    return (uint32_t)((value >> TM_RAPL_PL2_SHIFT) & TM_RAPL_POWER_FIELD_MASK);
}

static inline uint64_t tm_rapl_with_power_limits(
    uint64_t original,
    uint32_t pl1_raw,
    uint32_t pl2_raw
) {
    const uint64_t pl1_clear = TM_RAPL_POWER_FIELD_MASK
        | TM_RAPL_PL1_ENABLE
        | TM_RAPL_PL1_CLAMP;
    const uint64_t pl2_clear = (TM_RAPL_POWER_FIELD_MASK << TM_RAPL_PL2_SHIFT)
        | TM_RAPL_PL2_ENABLE
        | TM_RAPL_PL2_CLAMP;
    uint64_t result = original & ~(pl1_clear | pl2_clear | TM_RAPL_LOCK);
    result |= (uint64_t)(pl1_raw & TM_RAPL_POWER_FIELD_MASK);
    result |= ((uint64_t)(pl2_raw & TM_RAPL_POWER_FIELD_MASK) << TM_RAPL_PL2_SHIFT);
    result |= TM_RAPL_PL1_ENABLE | TM_RAPL_PL1_CLAMP;
    result |= TM_RAPL_PL2_ENABLE | TM_RAPL_PL2_CLAMP;
    return result;
}

#endif
