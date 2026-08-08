#include "TurboMac.h"
#include "TurboMacUserClient.h"

#include <IOKit/IOLib.h>
#include <IOKit/IOUserClient.h>
#include <i386/proc_reg.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <mach/kmod.h>
#include <mach/mach_time.h>

#include "../Shared/HWPCodec.h"
#include "../Shared/RAPLCodec.h"
#include "../Shared/DriverValidation.h"

#define super IOService
OSDefineMetaClassAndStructors(TurboMac, IOService);

extern "C" void mp_rendezvous_no_intrs(void (*action)(void *), void *argument);
extern "C" int cpu_number(void);

namespace {

constexpr uint64_t kLimitReadbackMask = TM_RAPL_POWER_FIELD_MASK
    | TM_RAPL_PL1_ENABLE
    | TM_RAPL_PL1_CLAMP
    | (TM_RAPL_POWER_FIELD_MASK << TM_RAPL_PL2_SHIFT)
    | TM_RAPL_PL2_ENABLE
    | TM_RAPL_PL2_CLAMP
    | TM_RAPL_LOCK;

uint32_t minimumNonZero(uint32_t first, uint32_t second) {
    if (first == 0U) {
        return second;
    }
    if (second == 0U) {
        return first;
    }
    return first < second ? first : second;
}

} // namespace

bool TurboMac::init(OSDictionary *dictionary) {
    if (!super::init(dictionary)) {
        return false;
    }

    lock_ = IOLockAlloc();
    workLoop_ = nullptr;
    watchdog_ = nullptr;
    clientReserved_ = false;
    supported_ = false;
    captured_ = false;
    state_ = kTurboMacDriverPassive;
    restoreReason_ = kTurboMacRestoreNone;
    powerExponent_ = 0U;
    energyExponent_ = 0U;
    floorMW_ = 0U;
    capabilityMaximumMW_ = 0U;
    thermalSpecMW_ = 0U;
    ceilingPL1MW_ = 0U;
    ceilingPL2MW_ = 0U;
    appliedPL1MW_ = 0U;
    appliedPL2MW_ = 0U;
    watchdogTimeoutMS_ = kWatchdogTimeoutMS;
    cpuFamily_ = 0U;
    cpuModel_ = 0U;
    cpuStepping_ = 0U;
    logicalCPUCount_ = 0U;
    unitsRaw_ = 0U;
    powerInfoRaw_ = 0U;
    capturedPackageLimit_ = 0U;
    capturedPowerControl_ = 0U;
    currentPowerControl_ = 0U;
    expectedPackageLimit_ = 0U;
    hwpCPUIDEAX_ = 0U;
    hwpMode_ = kTurboMacHWPModeInvalid;
    hwpPMEnableRaw_ = 0U;
    hwpCapabilitiesRaw_ = 0U;
    capturedHWPPackageRequest_ = 0U;
    expectedHWPPackageRequest_ = 0U;
    hwpFailSafeRequest_ = 0U;
    bzero(capturedHWPRequests_, sizeof(capturedHWPRequests_));
    bzero(expectedHWPRequests_, sizeof(expectedHWPRequests_));
    bzero(capturedHWPPresent_, sizeof(capturedHWPPresent_));
    hwpSupported_ = false;
    hwpEnabled_ = false;
    hwpEPPSupported_ = false;
    hwpFlexibleRequestFields_ = false;
    hwpPackageRequestSupported_ = false;
    hwpCaptured_ = false;
    hwpOverrideActive_ = false;
    hwpEnabledByTurboMac_ = false;
    hwpFailSafeActive_ = false;
    lastSequence_ = 0U;
    lastHeartbeat_ = 0U;

    return lock_ != nullptr;
}

bool TurboMac::start(IOService *provider) {
    if (!super::start(provider)) {
        return false;
    }

    workLoop_ = IOWorkLoop::workLoop();
    if (workLoop_ == nullptr) {
        super::stop(provider);
        return false;
    }

    watchdog_ = IOTimerEventSource::timerEventSource(this, watchdogFired);
    if (watchdog_ == nullptr || workLoop_->addEventSource(watchdog_) != kIOReturnSuccess) {
        if (watchdog_ != nullptr) {
            watchdog_->release();
            watchdog_ = nullptr;
        }
        workLoop_->release();
        workLoop_ = nullptr;
        super::stop(provider);
        return false;
    }

    IOLockLock(lock_);
    const bool initialized = initializeCapabilitiesLocked();
    if (initialized) {
        schedulePassiveGuardLocked();
    }
    IOLockUnlock(lock_);
    if (!initialized) {
        IOLog("TurboMac: unsupported CPU; service remains passive and cannot arm\n");
    } else {
        IOLog("TurboMac: passive RAPL governor ready for family 0x%x model 0x%x\n",
              cpuFamily_, cpuModel_);
    }

    registerService();
    return true;
}

void TurboMac::stop(IOService *provider) {
    if (lock_ != nullptr) {
        IOLockLock(lock_);
        if (captured_ || hwpEnabledByTurboMac_) {
            restoreLocked(kTurboMacRestoreDriverStop);
        }
        IOLockUnlock(lock_);
    }

    if (watchdog_ != nullptr) {
        watchdog_->cancelTimeout();
        if (workLoop_ != nullptr) {
            workLoop_->removeEventSource(watchdog_);
        }
        watchdog_->release();
        watchdog_ = nullptr;
    }
    if (workLoop_ != nullptr) {
        workLoop_->release();
        workLoop_ = nullptr;
    }

    super::stop(provider);
}

void TurboMac::free() {
    if (lock_ != nullptr) {
        IOLockFree(lock_);
        lock_ = nullptr;
    }
    super::free();
}

IOReturn TurboMac::newUserClient(
    task_t owningTask,
    void *securityID,
    UInt32 type,
    OSDictionary *properties,
    IOUserClient **handler
) {
    (void)properties;
    return newUserClient(owningTask, securityID, type, handler);
}

IOReturn TurboMac::newUserClient(
    task_t owningTask,
    void *securityID,
    UInt32 type,
    IOUserClient **handler
) {
    if (handler == nullptr || type != 0U) {
        return kIOReturnBadArgument;
    }
    *handler = nullptr;

    if (IOUserClient::clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator)
        != kIOReturnSuccess) {
        return kIOReturnNotPrivileged;
    }

    IOLockLock(lock_);
    const bool reserved = reserveClientLocked();
    IOLockUnlock(lock_);
    if (!reserved) {
        return kIOReturnExclusiveAccess;
    }

    TurboMacUserClient *client = new TurboMacUserClient;
    bool attached = false;
    bool started = false;
    if (client != nullptr && client->initWithTask(owningTask, securityID, type)) {
        attached = client->attach(this);
        if (attached) {
            started = client->start(this);
        }
    }
    if (!started) {
        if (client != nullptr) {
            if (attached) {
                client->detach(this);
            }
            client->release();
        }
        IOLockLock(lock_);
        releaseClientLocked();
        IOLockUnlock(lock_);
        return kIOReturnNoResources;
    }

    *handler = client;
    return kIOReturnSuccess;
}

IOReturn TurboMac::copyCapabilities(TurboMacCapabilities *output) {
    if (output == nullptr) {
        return kIOReturnBadArgument;
    }

    IOLockLock(lock_);
    if (state_ != kTurboMacDriverArmed && !hwpEnabled_) {
        initializeHWPLocked();
    }
    output->version = TURBOMAC_PROTOCOL_VERSION;
    output->size = sizeof(*output);
    output->cpu_family = cpuFamily_;
    output->cpu_model = cpuModel_;
    output->cpu_stepping = cpuStepping_;
    output->flags = 0U;
    if (supported_) {
        output->flags |= kTurboMacCapabilitySupportedCPU;
    }
    if (powerInfoRaw_ != 0U) {
        output->flags |= kTurboMacCapabilityPowerInfoValid;
    }
    if (hwpSupported_) {
        output->flags |= kTurboMacCapabilityHWPSupported;
    }
    if (hwpEnabled_) {
        output->flags |= kTurboMacCapabilityHWPEnabled;
    }
    const uint64_t packageLimit = supported_ ? rdmsr64(kMSRPackagePowerLimit) : 0U;
    bool allGuardEnabled = false;
    bool allGuardDisabled = false;
    uint64_t powerControl = 0U;
    if (supported_) {
        readPowerControlLocked(&allGuardEnabled, &allGuardDisabled, &powerControl);
    }
    if ((packageLimit & TM_RAPL_LOCK) != 0U) {
        output->flags |= kTurboMacCapabilityLimitLocked;
    }
    if (allGuardEnabled) {
        output->flags |= kTurboMacCapabilityBidirProchotEnabled;
    }
    output->rapl_power_exponent = powerExponent_;
    output->rapl_energy_exponent = energyExponent_;
    output->minimum_power_mw = floorMW_;
    output->maximum_power_mw = capabilityMaximumMW_;
    output->thermal_spec_power_mw = thermalSpecMW_;
    output->logical_cpu_count = logicalCPUCount_;
    output->rapl_units_raw = unitsRaw_;
    output->package_power_info_raw = powerInfoRaw_;
    output->current_package_limit_raw = packageLimit;
    output->current_power_control_raw = powerControl;
    output->hwp_cpuid_eax = hwpCPUIDEAX_;
    output->reserved0 = 0U;
    output->hwp_pm_enable_raw = hwpPMEnableRaw_;
    output->hwp_capabilities_raw = hwpCapabilitiesRaw_;
    output->current_hwp_package_request_raw = hwpPackageRequestSupported_ && hwpEnabled_
        ? rdmsr64(kMSRHWPPackageRequest)
        : 0U;
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

IOReturn TurboMac::copyTelemetry(TurboMacTelemetry *output) {
    if (output == nullptr) {
        return kIOReturnBadArgument;
    }

    IOLockLock(lock_);
    if (!supported_) {
        IOLockUnlock(lock_);
        return kIOReturnUnsupported;
    }
    output->version = TURBOMAC_PROTOCOL_VERSION;
    output->size = sizeof(*output);
    output->mach_absolute_time = readAbsoluteTime();
    output->package_energy_raw = rdmsr64(kMSRPackageEnergyStatus) & UINT64_C(0xffffffff);
    output->package_limit_raw = rdmsr64(kMSRPackagePowerLimit);
    output->power_control_raw = currentPowerControl_;
    output->applied_pl1_mw = appliedPL1MW_;
    output->applied_pl2_mw = appliedPL2MW_;
    output->driver_state = state_;
    output->restore_reason = restoreReason_;
    output->last_sequence = lastSequence_;
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

IOReturn TurboMac::arm(const TurboMacLimitRequest *request) {
    IOLockLock(lock_);
    if (!supported_) {
        IOLockUnlock(lock_);
        return kIOReturnUnsupported;
    }
    if (captured_ || state_ == kTurboMacDriverArmed) {
        IOLockUnlock(lock_);
        return kIOReturnBusy;
    }
    if (!validateRequestLocked(request, true)) {
        restoreReason_ = kTurboMacRestoreInvalidRequest;
        IOLockUnlock(lock_);
        return kIOReturnBadArgument;
    }

    const uint64_t packageLimit = rdmsr64(kMSRPackagePowerLimit);
    bool allGuardEnabled = false;
    bool allGuardDisabled = false;
    uint64_t powerControl = 0U;
    const bool powerControlReadable = readPowerControlLocked(
        &allGuardEnabled, &allGuardDisabled, &powerControl
    );
    if (!hwpEnabled_) {
        initializeHWPLocked();
    }
    const bool hwpWasEnabled = hwpEnabled_;
    const bool hwpModeValid = hwpWasEnabled
        ? request->hwp_mode == kTurboMacHWPReleaseMaximum
        : request->hwp_mode == kTurboMacHWPBootstrapAndReleaseMaximum;
    if ((packageLimit & TM_RAPL_LOCK) != 0U
        || !powerControlReadable
        || !allGuardEnabled
        || !hwpSupported_
        || !hwpModeValid
        || (packageLimit & TM_RAPL_PL1_ENABLE) == 0U
        || (packageLimit & TM_RAPL_PL2_ENABLE) == 0U) {
        restoreReason_ = kTurboMacRestoreInvalidRequest;
        IOLockUnlock(lock_);
        return kIOReturnNotPermitted;
    }

    if (hwpWasEnabled
        && (!captureHWPStateLocked()
            || !hwpMaximumRestrictedLocked(
                capturedHWPRequests_,
                capturedHWPPresent_,
                capturedHWPPackageRequest_
            ))) {
        hwpCaptured_ = false;
        bzero(capturedHWPPresent_, sizeof(capturedHWPPresent_));
        restoreReason_ = kTurboMacRestoreInvalidRequest;
        IOLockUnlock(lock_);
        return kIOReturnNotPermitted;
    }

    capturedPackageLimit_ = packageLimit;
    capturedPowerControl_ = powerControl;
    ceilingPL1MW_ = tm_rapl_raw_to_mw(tm_rapl_pl1_raw(packageLimit), powerExponent_);
    ceilingPL2MW_ = tm_rapl_raw_to_mw(tm_rapl_pl2_raw(packageLimit), powerExponent_);
    captured_ = true;

    uint32_t bootstrapLimitMW = request->pl1_mw;
    if (!hwpWasEnabled) {
        const uint32_t conservativeLimitMW = floorMW_ > kHWPBootstrapLimitMW
            ? floorMW_
            : kHWPBootstrapLimitMW;
        if (bootstrapLimitMW > conservativeLimitMW) {
            bootstrapLimitMW = conservativeLimitMW;
        }
    }
    if (!validateRequestLocked(request, true)
        || !installLimitsLocked(
            bootstrapLimitMW,
            hwpWasEnabled ? request->pl2_mw : bootstrapLimitMW
        )
        || (!hwpWasEnabled && !bootstrapHWPLocked())
        || (!hwpWasEnabled
            && (bootstrapLimitMW != request->pl1_mw
                || bootstrapLimitMW != request->pl2_mw)
            && !installLimitsLocked(request->pl1_mw, request->pl2_mw))) {
        restoreLocked(kTurboMacRestoreReadbackMismatch);
        IOLockUnlock(lock_);
        return kIOReturnError;
    }

    if (!applyHWPMaximumLocked(request->hwp_mode)
        || !setPowerControlGuardLocked(false)) {
        restoreLocked(kTurboMacRestoreReadbackMismatch);
        IOLockUnlock(lock_);
        return kIOReturnError;
    }

    state_ = kTurboMacDriverArmed;
    restoreReason_ = kTurboMacRestoreNone;
    lastSequence_ = request->sequence;
    watchdogTimeoutMS_ = request->watchdog_timeout_ms;
    lastHeartbeat_ = readAbsoluteTime();
    scheduleWatchdogLocked();
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

IOReturn TurboMac::update(const TurboMacLimitRequest *request) {
    IOLockLock(lock_);
    if (!captured_ || state_ != kTurboMacDriverArmed) {
        IOLockUnlock(lock_);
        return kIOReturnNotOpen;
    }
    if (!validateRequestLocked(request, false)
        || request->hwp_mode != kTurboMacHWPReleaseMaximum) {
        restoreLocked(kTurboMacRestoreInvalidRequest);
        IOLockUnlock(lock_);
        return kIOReturnBadArgument;
    }

    if (!verifyArmedStateLocked()) {
        restoreLocked(kTurboMacRestoreReadbackMismatch);
        IOLockUnlock(lock_);
        return kIOReturnError;
    }

    if (!validateRequestLocked(request, false) || !installLimitsLocked(request->pl1_mw, request->pl2_mw)) {
        restoreLocked(kTurboMacRestoreReadbackMismatch);
        IOLockUnlock(lock_);
        return kIOReturnError;
    }

    lastSequence_ = request->sequence;
    lastHeartbeat_ = readAbsoluteTime();
    scheduleWatchdogLocked();
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

IOReturn TurboMac::disarm(const TurboMacCommandRequest *request) {
    IOLockLock(lock_);
    if (request == nullptr
        || request->version != TURBOMAC_PROTOCOL_VERSION
        || request->size != sizeof(*request)
        || request->sequence <= lastSequence_) {
        restoreLocked(kTurboMacRestoreInvalidRequest);
        IOLockUnlock(lock_);
        return kIOReturnBadArgument;
    }
    lastSequence_ = request->sequence;
    const bool restored = restoreLocked(kTurboMacRestoreRequested);
    IOLockUnlock(lock_);
    return restored ? kIOReturnSuccess : kIOReturnError;
}

IOReturn TurboMac::copyStatus(TurboMacDriverStatus *output) {
    if (output == nullptr) {
        return kIOReturnBadArgument;
    }
    IOLockLock(lock_);
    output->version = TURBOMAC_PROTOCOL_VERSION;
    output->size = sizeof(*output);
    output->driver_state = state_;
    output->restore_reason = restoreReason_;
    output->applied_pl1_mw = appliedPL1MW_;
    output->applied_pl2_mw = appliedPL2MW_;
    output->floor_mw = floorMW_;
    output->ceiling_pl1_mw = ceilingPL1MW_;
    output->ceiling_pl2_mw = ceilingPL2MW_;
    output->watchdog_timeout_ms = watchdogTimeoutMS_;
    output->last_sequence = lastSequence_;
    output->captured_package_limit_raw = capturedPackageLimit_;
    output->captured_power_control_raw = capturedPowerControl_;
    output->current_package_limit_raw = supported_ ? rdmsr64(kMSRPackagePowerLimit) : 0U;
    if (supported_) {
        bool allEnabled = false;
        bool allDisabled = false;
        uint64_t representative = 0U;
        if (readPowerControlLocked(&allEnabled, &allDisabled, &representative)) {
            currentPowerControl_ = representative;
        }
    }
    output->current_power_control_raw = currentPowerControl_;
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

IOReturn TurboMac::copyHWPStatus(TurboMacHWPStatus *output) {
    if (output == nullptr) {
        return kIOReturnBadArgument;
    }

    IOLockLock(lock_);
    if (state_ != kTurboMacDriverArmed && !hwpEnabled_) {
        initializeHWPLocked();
    }
    output->version = TURBOMAC_PROTOCOL_VERSION;
    output->size = sizeof(*output);
    output->flags = 0U;
    if (hwpSupported_) {
        output->flags |= kTurboMacHWPStatusSupported;
    }
    if (hwpEnabled_) {
        output->flags |= kTurboMacHWPStatusEnabled;
    }
    if (hwpFlexibleRequestFields_) {
        output->flags |= kTurboMacHWPStatusFlexibleRequestFields;
    }
    if (hwpPackageRequestSupported_) {
        output->flags |= kTurboMacHWPStatusPackageControlSupported;
    }
    if (hwpOverrideActive_) {
        output->flags |= kTurboMacHWPStatusOverrideActive;
    }
    if (hwpCaptured_) {
        output->flags |= kTurboMacHWPStatusCaptureValid;
    }
    if (hwpEnabledByTurboMac_) {
        output->flags |= kTurboMacHWPStatusEnabledByTurboMac
            | kTurboMacHWPStatusNativeRestoreRequiresReboot;
    }
    output->hwp_cpuid_eax = hwpCPUIDEAX_;
    output->hwp_mode = hwpMode_;
    output->hwp_pm_enable_raw = hwpPMEnableRaw_;
    output->hwp_capabilities_raw = hwpCapabilitiesRaw_;
    output->captured_package_request_raw = hwpCaptured_
        ? capturedHWPPackageRequest_
        : 0U;
    output->current_package_request_raw = hwpPackageRequestSupported_ && hwpEnabled_
        ? rdmsr64(kMSRHWPPackageRequest)
        : 0U;
    output->failsafe_request_raw = hwpFailSafeRequest_;

    uint64_t current[kMaximumLogicalCPUs] = {};
    uint8_t present[kMaximumLogicalCPUs] = {};
    uint32_t count = 0U;
    const bool readbackValid = hwpEnabled_
        && readHWPRequestsLocked(current, present, &count);
    if (readbackValid) {
        output->flags |= kTurboMacHWPStatusReadbackValid;
        bool failSafeReadback = hwpFailSafeActive_
            && count == logicalCPUCount_;
        for (uint32_t index = 0U;
             failSafeReadback && index < kMaximumLogicalCPUs;
             ++index) {
            if (present[index] != 0U
                && current[index] != hwpFailSafeRequest_) {
                failSafeReadback = false;
            }
        }
        if (failSafeReadback) {
            output->flags |= kTurboMacHWPStatusFailsafeRequestActive;
        }
        if (hwpMaximumRestrictedLocked(
                current, present, output->current_package_request_raw
            )) {
            output->flags |= kTurboMacHWPStatusMaximumRestricted;
        }
    }
    output->logical_cpu_count = readbackValid ? count : 0U;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        TurboMacHWPEntry &entry = output->cpus[index];
        entry.cpu_index = index;
        entry.flags = 0U;
        entry.captured_request_raw = hwpCaptured_ && capturedHWPPresent_[index] != 0U
            ? capturedHWPRequests_[index]
            : 0U;
        entry.current_request_raw = readbackValid && present[index] != 0U
            ? current[index]
            : 0U;
        if (readbackValid && present[index] != 0U) {
            entry.flags |= kTurboMacHWPEntryPresent;
            if (tm_hwp_package_control(current[index])) {
                entry.flags |= kTurboMacHWPEntryPackageControl;
            }
            if (!tm_hwp_package_control(current[index])
                || (hwpFlexibleRequestFields_ && tm_hwp_maximum_valid(current[index]))) {
                entry.flags |= kTurboMacHWPEntryMaximumFromLocalRequest;
            }
        }
        if (hwpOverrideActive_ && capturedHWPPresent_[index] != 0U
            && expectedHWPRequests_[index] != capturedHWPRequests_[index]) {
            entry.flags |= kTurboMacHWPEntryModified;
        }
    }
    IOLockUnlock(lock_);
    return kIOReturnSuccess;
}

void TurboMac::invalidClientCommand() {
    IOLockLock(lock_);
    restoreLocked(kTurboMacRestoreInvalidRequest);
    IOLockUnlock(lock_);
}

void TurboMac::clientDisconnected() {
    IOLockLock(lock_);
    restoreLocked(kTurboMacRestoreClientClosed);
    releaseClientLocked();
    IOLockUnlock(lock_);
}

void TurboMac::watchdogFired(OSObject *owner, IOTimerEventSource *sender) {
    TurboMac *driver = OSDynamicCast(TurboMac, owner);
    if (driver == nullptr || driver->lock_ == nullptr) {
        return;
    }

    IOLockLock(driver->lock_);
    if (driver->captured_ && driver->state_ == kTurboMacDriverArmed) {
        const uint64_t now = driver->readAbsoluteTime();
        if (driver->watchdogExpiredLocked(now)) {
            driver->restoreLocked(kTurboMacRestoreWatchdog);
        } else if (!driver->verifyArmedStateLocked()) {
            driver->restoreLocked(kTurboMacRestoreReadbackMismatch);
        } else {
            sender->setTimeoutMS(kWatchdogPollMS);
        }
    } else if (driver->supported_) {
        if (!driver->hwpEnabled_) {
            driver->initializeHWPLocked();
        }
        const bool guardSafe = driver->ensurePassiveGuardLocked();
        const bool hwpSafe = !driver->hwpEnabledByTurboMac_
            || driver->verifyHWPFailSafeLocked()
            || driver->ensureHWPFailSafeLocked();
        if (!guardSafe || !hwpSafe) {
            driver->state_ = kTurboMacDriverFailsafe;
            driver->restoreReason_ = kTurboMacRestoreReadbackMismatch;
        }
        sender->setTimeoutMS(kPassiveGuardPollMS);
    }
    IOLockUnlock(driver->lock_);
}

bool TurboMac::initializeCapabilitiesLocked() {
    readCPUIdentity(&cpuFamily_, &cpuModel_, &cpuStepping_);
    if (cpuFamily_ != kSupportedFamily || cpuModel_ != kSupportedModel) {
        return false;
    }

    unitsRaw_ = rdmsr64(kMSRRAPLPowerUnit);
    powerInfoRaw_ = rdmsr64(kMSRPackagePowerInfo);
    powerExponent_ = (uint32_t)(unitsRaw_ & UINT64_C(0xf));
    energyExponent_ = (uint32_t)((unitsRaw_ >> 8U) & UINT64_C(0x1f));

    const uint32_t thermalRaw = (uint32_t)(powerInfoRaw_ & TM_RAPL_POWER_FIELD_MASK);
    const uint32_t minimumRaw = (uint32_t)((powerInfoRaw_ >> 16U) & TM_RAPL_POWER_FIELD_MASK);
    const uint32_t maximumRaw = (uint32_t)((powerInfoRaw_ >> 32U) & TM_RAPL_POWER_FIELD_MASK);
    thermalSpecMW_ = tm_rapl_raw_to_mw(thermalRaw, powerExponent_);
    floorMW_ = tm_rapl_raw_to_mw(minimumRaw, powerExponent_);
    capabilityMaximumMW_ = tm_rapl_raw_to_mw(maximumRaw, powerExponent_);

    if (floorMW_ == 0U) {
        floorMW_ = 1000U;
    }
    if (capabilityMaximumMW_ == 0U) {
        const uint64_t currentLimit = rdmsr64(kMSRPackagePowerLimit);
        const uint32_t currentPL1 = tm_rapl_raw_to_mw(
            tm_rapl_pl1_raw(currentLimit), powerExponent_
        );
        const uint32_t currentPL2 = tm_rapl_raw_to_mw(
            tm_rapl_pl2_raw(currentLimit), powerExponent_
        );
        capabilityMaximumMW_ = currentPL1 > currentPL2 ? currentPL1 : currentPL2;
    }
    supported_ = powerExponent_ < 31U
        && energyExponent_ < 32U
        && thermalSpecMW_ > 0U
        && capabilityMaximumMW_ >= floorMW_;
    initializeHWPLocked();
    if (supported_) {
        bool allEnabled = false;
        bool allDisabled = false;
        supported_ = readPowerControlLocked(
            &allEnabled, &allDisabled, &currentPowerControl_
        );
        if (supported_ && !allEnabled) {
            supported_ = setPowerControlGuardLocked(true);
            if (supported_) {
                IOLog("TurboMac: restored Apple's bidirectional PROCHOT guard for passive mode\n");
            }
        }
    }
    return supported_;
}

bool TurboMac::reserveClientLocked() {
    if (clientReserved_) {
        return false;
    }
    clientReserved_ = true;
    return true;
}

void TurboMac::releaseClientLocked() {
    clientReserved_ = false;
}

bool TurboMac::validateRequestLocked(const TurboMacLimitRequest *request, bool isArm) const {
    uint32_t pl1Ceiling = ceilingPL1MW_;
    uint32_t pl2Ceiling = ceilingPL2MW_;
    if (isArm && !captured_) {
        const uint64_t packageLimit = rdmsr64(kMSRPackagePowerLimit);
        pl1Ceiling = tm_rapl_raw_to_mw(tm_rapl_pl1_raw(packageLimit), powerExponent_);
        pl2Ceiling = tm_rapl_raw_to_mw(tm_rapl_pl2_raw(packageLimit), powerExponent_);
    }
    pl1Ceiling = minimumNonZero(pl1Ceiling, capabilityMaximumMW_);
    pl2Ceiling = minimumNonZero(pl2Ceiling, capabilityMaximumMW_);
    return tm_validate_limit_request(
        request,
        lastSequence_,
        floorMW_,
        pl1Ceiling,
        pl2Ceiling,
        kWatchdogTimeoutMS
    );
}

bool TurboMac::installLimitsLocked(uint32_t pl1MW, uint32_t pl2MW) {
    const uint64_t current = rdmsr64(kMSRPackagePowerLimit);
    if ((current & TM_RAPL_LOCK) != 0U) {
        return false;
    }
    const uint32_t pl1Raw = tm_rapl_mw_to_raw(pl1MW, powerExponent_);
    const uint32_t pl2Raw = tm_rapl_mw_to_raw(pl2MW, powerExponent_);
    if (captured_
        && (pl1Raw > tm_rapl_pl1_raw(capturedPackageLimit_)
            || pl2Raw > tm_rapl_pl2_raw(capturedPackageLimit_))) {
        return false;
    }
    const uint64_t requested = tm_rapl_with_power_limits(current, pl1Raw, pl2Raw);
    if ((requested & TM_RAPL_LOCK) != 0U) {
        return false;
    }

    wrmsr64(kMSRPackagePowerLimit, requested);
    if (!verifyLimitsLocked(requested)) {
        return false;
    }
    expectedPackageLimit_ = requested;
    appliedPL1MW_ = tm_rapl_raw_to_mw(pl1Raw, powerExponent_);
    appliedPL2MW_ = tm_rapl_raw_to_mw(pl2Raw, powerExponent_);
    return true;
}

bool TurboMac::verifyLimitsLocked(uint64_t expected) const {
    const uint64_t readback = rdmsr64(kMSRPackagePowerLimit);
    return (readback & kLimitReadbackMask) == (expected & kLimitReadbackMask)
        && (readback & TM_RAPL_LOCK) == 0U;
}

void TurboMac::powerControlOnCPU(void *argument) {
    PowerControlBroadcast *broadcast =
        static_cast<PowerControlBroadcast *>(argument);
    uint64_t value = rdmsr64(kMSRPowerControl);
    if (broadcast->operation == kPowerControlClearGuard) {
        value &= ~kEnableBidirProchot;
        wrmsr64(kMSRPowerControl, value);
        value = rdmsr64(kMSRPowerControl);
    } else if (broadcast->operation == kPowerControlSetGuard) {
        value |= kEnableBidirProchot;
        wrmsr64(kMSRPowerControl, value);
        value = rdmsr64(kMSRPowerControl);
    }

    const SInt32 index = OSIncrementAtomic(&broadcast->count);
    if (index >= 0 && (uint32_t)index < kMaximumLogicalCPUs) {
        broadcast->values[index] = value;
    } else {
        OSIncrementAtomic(&broadcast->overflow);
    }
}

bool TurboMac::readPowerControlLocked(
    bool *allEnabled,
    bool *allDisabled,
    uint64_t *representative
) {
    if (allEnabled == nullptr || allDisabled == nullptr || representative == nullptr) {
        return false;
    }
    PowerControlBroadcast broadcast = {};
    broadcast.operation = kPowerControlRead;
    mp_rendezvous_no_intrs(powerControlOnCPU, &broadcast);
    if (broadcast.overflow != 0
        || broadcast.count <= 0
        || (uint32_t)broadcast.count > kMaximumLogicalCPUs) {
        return false;
    }

    *allEnabled = true;
    *allDisabled = true;
    *representative = broadcast.values[0];
    for (SInt32 index = 0; index < broadcast.count; ++index) {
        const bool enabled =
            (broadcast.values[index] & kEnableBidirProchot) != 0U;
        *allEnabled = *allEnabled && enabled;
        *allDisabled = *allDisabled && !enabled;
    }
    logicalCPUCount_ = (uint32_t)broadcast.count;
    currentPowerControl_ = *representative;
    return true;
}

bool TurboMac::ensurePassiveGuardLocked() {
    bool allEnabled = false;
    bool allDisabled = false;
    uint64_t representative = 0U;
    if (!readPowerControlLocked(&allEnabled, &allDisabled, &representative)) {
        return false;
    }
    return allEnabled || setPowerControlGuardLocked(true);
}

bool TurboMac::setPowerControlGuardLocked(bool enabled) {
    PowerControlBroadcast broadcast = {};
    broadcast.operation = enabled
        ? kPowerControlSetGuard
        : kPowerControlClearGuard;
    mp_rendezvous_no_intrs(powerControlOnCPU, &broadcast);
    if (broadcast.overflow != 0
        || broadcast.count <= 0
        || (uint32_t)broadcast.count > kMaximumLogicalCPUs) {
        return false;
    }
    for (SInt32 index = 0; index < broadcast.count; ++index) {
        const bool readbackEnabled =
            (broadcast.values[index] & kEnableBidirProchot) != 0U;
        if (readbackEnabled != enabled) {
            return false;
        }
    }
    logicalCPUCount_ = (uint32_t)broadcast.count;
    currentPowerControl_ = broadcast.values[0];
    return true;
}

bool TurboMac::initializeHWPLocked() {
    uint32_t maximumLeaf = 0U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    __asm__ volatile("cpuid"
                     : "+a"(maximumLeaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    if (maximumLeaf < 6U) {
        return false;
    }

    uint32_t eax = 6U;
    ebx = 0U;
    ecx = 0U;
    edx = 0U;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    hwpCPUIDEAX_ = eax;
    hwpSupported_ = (eax & kHWPFeatureSupported) != 0U;
    hwpEPPSupported_ = (eax & kHWPFeatureEPP) != 0U;
    hwpPackageRequestSupported_ = (eax & kHWPFeaturePackageRequest) != 0U;
    hwpFlexibleRequestFields_ = (eax & kHWPFeatureFlexibleRequestFields) != 0U;
    if (!hwpSupported_) {
        return false;
    }

    hwpPMEnableRaw_ = rdmsr64(kMSRHWPEnable);
    hwpEnabled_ = (hwpPMEnableRaw_ & kHWPEnable) != 0U;
    if (!hwpEnabled_) {
        return false;
    }

    hwpCapabilitiesRaw_ = rdmsr64(kMSRHWPCapabilities);
    if (!tm_hwp_capabilities_sane(hwpCapabilitiesRaw_)) {
        IOLog("TurboMac: HWP capability bounds failed validation; governor will remain passive\n");
        return false;
    }
    hwpFailSafeRequest_ = tm_hwp_failsafe_request(
        hwpCapabilitiesRaw_, hwpEPPSupported_
    );

    uint64_t requests[kMaximumLogicalCPUs] = {};
    uint8_t present[kMaximumLogicalCPUs] = {};
    uint32_t count = 0U;
    if (!readHWPRequestsLocked(requests, present, &count)) {
        IOLog("TurboMac: could not read every logical CPU HWP request; governor will remain passive\n");
        return false;
    }
    logicalCPUCount_ = count;
    return true;
}

void TurboMac::hwpRequestOnCPU(void *argument) {
    HWPRequestBroadcast *broadcast = static_cast<HWPRequestBroadcast *>(argument);
    const int cpu = cpu_number();
    OSIncrementAtomic(&broadcast->count);
    if (cpu < 0 || (uint32_t)cpu >= kMaximumLogicalCPUs
        || broadcast->present[cpu] != 0U) {
        OSIncrementAtomic(&broadcast->overflow);
        return;
    }

    uint64_t value = rdmsr64(kMSRHWPRequest);
    if (broadcast->operation == kHWPRequestWrite) {
        if (broadcast->targetPresent[cpu] == 0U) {
            OSIncrementAtomic(&broadcast->overflow);
            return;
        }
        wrmsr64(kMSRHWPRequest, broadcast->targets[cpu]);
        value = rdmsr64(kMSRHWPRequest);
    }
    broadcast->values[cpu] = value;
    broadcast->present[cpu] = 1U;
}

bool TurboMac::readHWPRequestsLocked(
    uint64_t values[kMaximumLogicalCPUs],
    uint8_t present[kMaximumLogicalCPUs],
    uint32_t *count
) const {
    if (!hwpSupported_ || !hwpEnabled_ || values == nullptr
        || present == nullptr || count == nullptr) {
        return false;
    }
    HWPRequestBroadcast broadcast = {};
    broadcast.operation = kHWPRequestRead;
    mp_rendezvous_no_intrs(hwpRequestOnCPU, &broadcast);
    if (broadcast.overflow != 0 || broadcast.count <= 0
        || (uint32_t)broadcast.count > kMaximumLogicalCPUs) {
        return false;
    }
    uint32_t presentCount = 0U;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        values[index] = broadcast.values[index];
        present[index] = broadcast.present[index];
        if (present[index] != 0U) {
            presentCount++;
        }
    }
    if (presentCount != (uint32_t)broadcast.count) {
        return false;
    }
    *count = presentCount;
    return true;
}

bool TurboMac::writeHWPRequestsLocked(
    const uint64_t values[kMaximumLogicalCPUs],
    const uint8_t present[kMaximumLogicalCPUs]
) const {
    if (!hwpSupported_ || !hwpEnabled_ || values == nullptr || present == nullptr) {
        return false;
    }
    HWPRequestBroadcast broadcast = {};
    broadcast.operation = kHWPRequestWrite;
    uint32_t targetCount = 0U;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        broadcast.targets[index] = values[index];
        broadcast.targetPresent[index] = present[index];
        if (present[index] != 0U) {
            targetCount++;
        }
    }
    mp_rendezvous_no_intrs(hwpRequestOnCPU, &broadcast);
    if (broadcast.overflow != 0 || broadcast.count <= 0
        || (uint32_t)broadcast.count != targetCount) {
        return false;
    }
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (present[index] != broadcast.present[index]) {
            return false;
        }
        if (present[index] != 0U && broadcast.values[index] != values[index]) {
            return false;
        }
    }
    return true;
}

bool TurboMac::captureHWPStateLocked() {
    hwpPMEnableRaw_ = hwpSupported_ ? rdmsr64(kMSRHWPEnable) : 0U;
    hwpEnabled_ = (hwpPMEnableRaw_ & kHWPEnable) != 0U;
    hwpCapabilitiesRaw_ = hwpEnabled_ ? rdmsr64(kMSRHWPCapabilities) : 0U;
    if (!hwpSupported_ || !hwpEnabled_
        || !tm_hwp_capabilities_sane(hwpCapabilitiesRaw_)) {
        return false;
    }
    uint32_t count = 0U;
    if (!readHWPRequestsLocked(
            capturedHWPRequests_, capturedHWPPresent_, &count
        )) {
        return false;
    }
    capturedHWPPackageRequest_ = hwpPackageRequestSupported_
        ? rdmsr64(kMSRHWPPackageRequest)
        : 0U;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (capturedHWPPresent_[index] != 0U
            && tm_hwp_package_control(capturedHWPRequests_[index])
            && !hwpPackageRequestSupported_) {
            return false;
        }
        expectedHWPRequests_[index] = capturedHWPRequests_[index];
    }
    expectedHWPPackageRequest_ = capturedHWPPackageRequest_;
    logicalCPUCount_ = count;
    hwpCaptured_ = true;
    hwpOverrideActive_ = false;
    hwpMode_ = kTurboMacHWPModeInvalid;
    return true;
}

bool TurboMac::bootstrapHWPLocked() {
    if (!captured_ || !hwpSupported_ || hwpEnabled_
        || !verifyLimitsLocked(expectedPackageLimit_)) {
        return false;
    }
    bool allGuardEnabled = false;
    bool allGuardDisabled = false;
    uint64_t powerControl = 0U;
    if (!readPowerControlLocked(
            &allGuardEnabled, &allGuardDisabled, &powerControl
        ) || !allGuardEnabled) {
        return false;
    }

    hwpPMEnableRaw_ = rdmsr64(kMSRHWPEnable);
    if ((hwpPMEnableRaw_ & kHWPEnable) == 0U) {
        wrmsr64(kMSRHWPEnable, kHWPEnable);
    }
    hwpPMEnableRaw_ = rdmsr64(kMSRHWPEnable);
    hwpEnabled_ = (hwpPMEnableRaw_ & kHWPEnable) != 0U;
    if (!hwpEnabled_) {
        return false;
    }
    hwpEnabledByTurboMac_ = true;

    if (!initializeHWPLocked()
        || tm_hwp_highest(hwpCapabilitiesRaw_) <= tm_hwp_lowest(hwpCapabilitiesRaw_)
        || !captureHWPStateLocked()
        || !ensureHWPFailSafeLocked()) {
        return false;
    }
    return verifyLimitsLocked(expectedPackageLimit_);
}

bool TurboMac::ensureHWPFailSafeLocked() {
    if (!hwpEnabledByTurboMac_ || !hwpEnabled_
        || !tm_hwp_capabilities_sane(hwpCapabilitiesRaw_)
        || tm_hwp_highest(hwpCapabilitiesRaw_) <= tm_hwp_lowest(hwpCapabilitiesRaw_)) {
        return false;
    }
    hwpFailSafeRequest_ = tm_hwp_failsafe_request(
        hwpCapabilitiesRaw_, hwpEPPSupported_
    );
    uint64_t current[kMaximumLogicalCPUs] = {};
    uint8_t present[kMaximumLogicalCPUs] = {};
    uint32_t count = 0U;
    if (!readHWPRequestsLocked(current, present, &count)) {
        return false;
    }
    uint64_t targets[kMaximumLogicalCPUs] = {};
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (present[index] != 0U) {
            targets[index] = hwpFailSafeRequest_;
        }
    }
    if (!writeHWPRequestsLocked(targets, present)) {
        return false;
    }
    logicalCPUCount_ = count;
    hwpMode_ = kTurboMacHWPModeInvalid;
    hwpOverrideActive_ = false;
    hwpFailSafeActive_ = true;
    return verifyHWPFailSafeLocked();
}

bool TurboMac::verifyHWPFailSafeLocked() const {
    if (!hwpEnabledByTurboMac_ || !hwpEnabled_ || !hwpFailSafeActive_) {
        return false;
    }
    uint64_t current[kMaximumLogicalCPUs] = {};
    uint8_t present[kMaximumLogicalCPUs] = {};
    uint32_t count = 0U;
    if (!readHWPRequestsLocked(current, present, &count)
        || count != logicalCPUCount_) {
        return false;
    }
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (present[index] != 0U && current[index] != hwpFailSafeRequest_) {
            return false;
        }
    }
    return true;
}

bool TurboMac::hwpMaximumRestrictedLocked(
    const uint64_t values[kMaximumLogicalCPUs],
    const uint8_t present[kMaximumLogicalCPUs],
    uint64_t packageRequest
) const {
    if (!hwpSupported_ || !hwpEnabled_ || values == nullptr || present == nullptr) {
        return false;
    }
    const uint32_t lowest = tm_hwp_lowest(hwpCapabilitiesRaw_);
    const uint32_t highest = tm_hwp_highest(hwpCapabilitiesRaw_);
    bool anyPresent = false;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (present[index] == 0U) {
            continue;
        }
        anyPresent = true;
        const uint32_t effective = tm_hwp_effective_maximum(
            values[index],
            packageRequest,
            hwpFlexibleRequestFields_
        );
        if (effective < lowest || effective >= highest) {
            return false;
        }
    }
    return anyPresent;
}

bool TurboMac::applyHWPMaximumLocked(uint32_t mode) {
    if (!hwpCaptured_ || hwpMode_ != kTurboMacHWPModeInvalid
        || (mode != kTurboMacHWPReleaseMaximum
            && mode != kTurboMacHWPBootstrapAndReleaseMaximum)) {
        return false;
    }
    const uint32_t highest = tm_hwp_highest(hwpCapabilitiesRaw_);
    bool packageMaximumNeeded = false;
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (capturedHWPPresent_[index] == 0U) {
            continue;
        }
        if (hwpEnabledByTurboMac_) {
            expectedHWPRequests_[index] = tm_hwp_active_request(
                hwpCapabilitiesRaw_, hwpEPPSupported_
            );
            continue;
        }
        const uint64_t captured = capturedHWPRequests_[index];
        if (tm_hwp_package_control(captured) && !hwpFlexibleRequestFields_) {
            packageMaximumNeeded = true;
            expectedHWPRequests_[index] = captured;
        } else {
            expectedHWPRequests_[index] = tm_hwp_with_local_maximum_override(
                captured, highest, hwpFlexibleRequestFields_
            );
        }
    }
    expectedHWPPackageRequest_ = packageMaximumNeeded
        ? tm_hwp_with_maximum(capturedHWPPackageRequest_, highest)
        : capturedHWPPackageRequest_;

    if (packageMaximumNeeded) {
        wrmsr64(kMSRHWPPackageRequest, expectedHWPPackageRequest_);
        if (rdmsr64(kMSRHWPPackageRequest) != expectedHWPPackageRequest_) {
            return false;
        }
    }
    if (!writeHWPRequestsLocked(expectedHWPRequests_, capturedHWPPresent_)) {
        return false;
    }
    hwpMode_ = mode;
    hwpOverrideActive_ = true;
    hwpFailSafeActive_ = false;
    return verifyHWPOverrideLocked();
}

bool TurboMac::verifyHWPOverrideLocked() const {
    if (!hwpCaptured_ || !hwpOverrideActive_
        || (hwpMode_ != kTurboMacHWPReleaseMaximum
            && hwpMode_ != kTurboMacHWPBootstrapAndReleaseMaximum)) {
        return false;
    }
    if (hwpPackageRequestSupported_
        && rdmsr64(kMSRHWPPackageRequest) != expectedHWPPackageRequest_) {
        return false;
    }
    uint64_t current[kMaximumLogicalCPUs] = {};
    uint8_t present[kMaximumLogicalCPUs] = {};
    uint32_t count = 0U;
    if (!readHWPRequestsLocked(current, present, &count)
        || count != logicalCPUCount_) {
        return false;
    }
    const uint32_t highest = tm_hwp_highest(hwpCapabilitiesRaw_);
    for (uint32_t index = 0U; index < kMaximumLogicalCPUs; ++index) {
        if (present[index] != capturedHWPPresent_[index]) {
            return false;
        }
        if (present[index] != 0U
            && (current[index] != expectedHWPRequests_[index]
                || tm_hwp_effective_maximum(
                       current[index],
                       expectedHWPPackageRequest_,
                       hwpFlexibleRequestFields_
                   ) != highest)) {
            return false;
        }
    }
    return true;
}

bool TurboMac::restoreHWPLocked() {
    if (hwpEnabledByTurboMac_) {
        hwpOverrideActive_ = false;
        hwpMode_ = kTurboMacHWPModeInvalid;
        hwpFailSafeActive_ = false;
        return ensureHWPFailSafeLocked();
    }
    if (!hwpCaptured_) {
        return true;
    }
    bool packageRestored = true;
    if (hwpPackageRequestSupported_) {
        wrmsr64(kMSRHWPPackageRequest, capturedHWPPackageRequest_);
        packageRestored = rdmsr64(kMSRHWPPackageRequest)
            == capturedHWPPackageRequest_;
    }
    const bool requestsRestored = writeHWPRequestsLocked(
        capturedHWPRequests_, capturedHWPPresent_
    );
    hwpOverrideActive_ = false;
    hwpMode_ = kTurboMacHWPModeInvalid;
    return packageRestored && requestsRestored;
}

bool TurboMac::verifyArmedStateLocked() {
    bool allEnabled = false;
    bool allDisabled = false;
    uint64_t currentControl = 0U;
    const bool guardValid = readPowerControlLocked(
        &allEnabled, &allDisabled, &currentControl
    ) && allDisabled;
    return guardValid
        && verifyLimitsLocked(expectedPackageLimit_)
        && verifyHWPOverrideLocked();
}

bool TurboMac::restoreLocked(uint32_t reason) {
    if (watchdog_ != nullptr) {
        watchdog_->cancelTimeout();
    }
    if (!captured_) {
        const bool guardRestored = supported_ && ensurePassiveGuardLocked();
        const bool hwpRestored = !hwpEnabledByTurboMac_
            || ensureHWPFailSafeLocked();
        state_ = guardRestored && hwpRestored
            && reason == kTurboMacRestoreRequested
            ? kTurboMacDriverPassive
            : kTurboMacDriverFailsafe;
        restoreReason_ = reason;
        schedulePassiveGuardLocked();
        return guardRestored && hwpRestored;
    }

    const bool guardRestored = setPowerControlGuardLocked(true);
    const bool hwpRestored = restoreHWPLocked();

    bool limitsRestored = false;
    const uint64_t currentLimit = rdmsr64(kMSRPackagePowerLimit);
    if (hwpRestored && (currentLimit & TM_RAPL_LOCK) == 0U) {
        wrmsr64(kMSRPackagePowerLimit, capturedPackageLimit_ & ~TM_RAPL_LOCK);
        limitsRestored = verifyLimitsLocked(capturedPackageLimit_ & ~TM_RAPL_LOCK);
    }

    captured_ = false;
    hwpCaptured_ = false;
    bzero(capturedHWPPresent_, sizeof(capturedHWPPresent_));
    appliedPL1MW_ = 0U;
    appliedPL2MW_ = 0U;
    expectedPackageLimit_ = 0U;
    lastHeartbeat_ = 0U;
    state_ = (guardRestored && hwpRestored && limitsRestored
              && reason == kTurboMacRestoreRequested)
        ? kTurboMacDriverPassive
        : kTurboMacDriverFailsafe;
    restoreReason_ = reason;
    schedulePassiveGuardLocked();
    return guardRestored && hwpRestored && limitsRestored;
}

void TurboMac::scheduleWatchdogLocked() {
    if (watchdog_ != nullptr) {
        watchdog_->setTimeoutMS(kWatchdogPollMS);
    }
}

void TurboMac::schedulePassiveGuardLocked() {
    if (watchdog_ != nullptr && supported_ && state_ != kTurboMacDriverArmed) {
        watchdog_->setTimeoutMS(kPassiveGuardPollMS);
    }
}

uint64_t TurboMac::readAbsoluteTime() const {
    return mach_absolute_time();
}

bool TurboMac::watchdogExpiredLocked(uint64_t now) const {
    if (lastHeartbeat_ == 0U || now < lastHeartbeat_) {
        return true;
    }
    uint64_t elapsedNS = 0U;
    absolutetime_to_nanoseconds(now - lastHeartbeat_, &elapsedNS);
    return tm_watchdog_expired(elapsedNS, watchdogTimeoutMS_);
}

void TurboMac::readCPUIdentity(uint32_t *family, uint32_t *model, uint32_t *stepping) {
    uint32_t eax = 1U;
    uint32_t ebx = 0U;
    uint32_t ecx = 0U;
    uint32_t edx = 0U;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    const uint32_t baseFamily = (eax >> 8U) & 0xfU;
    const uint32_t extendedFamily = (eax >> 20U) & 0xffU;
    const uint32_t baseModel = (eax >> 4U) & 0xfU;
    const uint32_t extendedModel = (eax >> 16U) & 0xfU;
    *family = baseFamily == 0xfU ? baseFamily + extendedFamily : baseFamily;
    *model = (baseFamily == 0x6U || baseFamily == 0xfU)
        ? baseModel | (extendedModel << 4U)
        : baseModel;
    *stepping = eax & 0xfU;
}

extern "C" kern_return_t TurboMacModuleStart(kmod_info_t *, void *) {
    return KERN_SUCCESS;
}

extern "C" kern_return_t TurboMacModuleStop(kmod_info_t *, void *) {
    return KERN_SUCCESS;
}

extern "C" kmod_info_t kmod_info = {
    0,
    KMOD_INFO_VERSION,
    UINT32_MAX,
    "com.parham.turbomac.driver",
    "2.2.0",
    -1,
    0,
    0,
    0,
    0,
    TurboMacModuleStart,
    TurboMacModuleStop,
};
