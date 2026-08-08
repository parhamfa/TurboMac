#ifndef TURBOMAC_PROTOCOL_H
#define TURBOMAC_PROTOCOL_H

#include <stdint.h>

#define TURBOMAC_SERVICE_NAME "TurboMac"
#define TURBOMAC_DRIVER_BUNDLE_ID "com.parham.turbomac.driver"
#define TURBOMAC_PROTOCOL_VERSION 3U
#define TURBOMAC_MAX_LOGICAL_CPUS 64U

enum TurboMacSelector {
    kTurboMacSelectorGetCapabilities = 0,
    kTurboMacSelectorReadTelemetry = 1,
    kTurboMacSelectorArm = 2,
    kTurboMacSelectorUpdate = 3,
    kTurboMacSelectorDisarm = 4,
    kTurboMacSelectorGetStatus = 5,
    kTurboMacSelectorGetHWPStatus = 6,
    kTurboMacSelectorCount = 7,
};

enum TurboMacDriverState {
    kTurboMacDriverPassive = 0,
    kTurboMacDriverArmed = 1,
    kTurboMacDriverFailsafe = 2,
};

enum TurboMacRestoreReason {
    kTurboMacRestoreNone = 0,
    kTurboMacRestoreRequested = 1,
    kTurboMacRestoreClientClosed = 2,
    kTurboMacRestoreWatchdog = 3,
    kTurboMacRestoreReadbackMismatch = 4,
    kTurboMacRestoreInvalidRequest = 5,
    kTurboMacRestoreDriverStop = 6,
};

enum TurboMacCapabilityFlag {
    kTurboMacCapabilitySupportedCPU = 1U << 0,
    kTurboMacCapabilityPowerInfoValid = 1U << 1,
    kTurboMacCapabilityLimitLocked = 1U << 2,
    kTurboMacCapabilityBidirProchotEnabled = 1U << 3,
    kTurboMacCapabilityHWPSupported = 1U << 4,
    kTurboMacCapabilityHWPEnabled = 1U << 5,
};

enum TurboMacHWPMode {
    kTurboMacHWPModeInvalid = 0,
    kTurboMacHWPReleaseMaximum = 1,
    kTurboMacHWPBootstrapAndReleaseMaximum = 2,
};

enum TurboMacHWPStatusFlag {
    kTurboMacHWPStatusSupported = 1U << 0,
    kTurboMacHWPStatusEnabled = 1U << 1,
    kTurboMacHWPStatusFlexibleRequestFields = 1U << 2,
    kTurboMacHWPStatusPackageControlSupported = 1U << 3,
    kTurboMacHWPStatusMaximumRestricted = 1U << 4,
    kTurboMacHWPStatusOverrideActive = 1U << 5,
    kTurboMacHWPStatusCaptureValid = 1U << 6,
    kTurboMacHWPStatusReadbackValid = 1U << 7,
    kTurboMacHWPStatusEnabledByTurboMac = 1U << 8,
    kTurboMacHWPStatusNativeRestoreRequiresReboot = 1U << 9,
    kTurboMacHWPStatusFailsafeRequestActive = 1U << 10,
};

enum TurboMacHWPEntryFlag {
    kTurboMacHWPEntryPresent = 1U << 0,
    kTurboMacHWPEntryPackageControl = 1U << 1,
    kTurboMacHWPEntryMaximumFromLocalRequest = 1U << 2,
    kTurboMacHWPEntryModified = 1U << 3,
};

typedef struct TurboMacCapabilities {
    uint32_t version;
    uint32_t size;
    uint32_t cpu_family;
    uint32_t cpu_model;
    uint32_t cpu_stepping;
    uint32_t flags;
    uint32_t rapl_power_exponent;
    uint32_t rapl_energy_exponent;
    uint32_t minimum_power_mw;
    uint32_t maximum_power_mw;
    uint32_t thermal_spec_power_mw;
    uint32_t logical_cpu_count;
    uint64_t rapl_units_raw;
    uint64_t package_power_info_raw;
    uint64_t current_package_limit_raw;
    uint64_t current_power_control_raw;
    uint32_t hwp_cpuid_eax;
    uint32_t reserved0;
    uint64_t hwp_pm_enable_raw;
    uint64_t hwp_capabilities_raw;
    uint64_t current_hwp_package_request_raw;
} TurboMacCapabilities;

typedef struct TurboMacTelemetry {
    uint32_t version;
    uint32_t size;
    uint64_t mach_absolute_time;
    uint64_t package_energy_raw;
    uint64_t package_limit_raw;
    uint64_t power_control_raw;
    uint32_t applied_pl1_mw;
    uint32_t applied_pl2_mw;
    uint32_t driver_state;
    uint32_t restore_reason;
    uint64_t last_sequence;
} TurboMacTelemetry;

typedef struct TurboMacLimitRequest {
    uint32_t version;
    uint32_t size;
    uint32_t pl1_mw;
    uint32_t pl2_mw;
    uint32_t watchdog_timeout_ms;
    uint32_t hwp_mode;
    uint64_t sequence;
} TurboMacLimitRequest;

typedef struct TurboMacCommandRequest {
    uint32_t version;
    uint32_t size;
    uint64_t sequence;
} TurboMacCommandRequest;

typedef struct TurboMacDriverStatus {
    uint32_t version;
    uint32_t size;
    uint32_t driver_state;
    uint32_t restore_reason;
    uint32_t applied_pl1_mw;
    uint32_t applied_pl2_mw;
    uint32_t floor_mw;
    uint32_t ceiling_pl1_mw;
    uint32_t ceiling_pl2_mw;
    uint32_t watchdog_timeout_ms;
    uint64_t last_sequence;
    uint64_t captured_package_limit_raw;
    uint64_t captured_power_control_raw;
    uint64_t current_package_limit_raw;
    uint64_t current_power_control_raw;
} TurboMacDriverStatus;

typedef struct TurboMacHWPEntry {
    uint32_t cpu_index;
    uint32_t flags;
    uint64_t captured_request_raw;
    uint64_t current_request_raw;
} TurboMacHWPEntry;

typedef struct TurboMacHWPStatus {
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    uint32_t logical_cpu_count;
    uint32_t hwp_cpuid_eax;
    uint32_t hwp_mode;
    uint64_t hwp_pm_enable_raw;
    uint64_t hwp_capabilities_raw;
    uint64_t captured_package_request_raw;
    uint64_t current_package_request_raw;
    uint64_t failsafe_request_raw;
    TurboMacHWPEntry cpus[TURBOMAC_MAX_LOGICAL_CPUS];
} TurboMacHWPStatus;

#endif
