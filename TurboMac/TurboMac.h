#ifndef TURBOMAC_DRIVER_H
#define TURBOMAC_DRIVER_H

#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>

#include "../Shared/TurboMacProtocol.h"

class IOUserClient;

class TurboMac : public IOService {
    OSDeclareDefaultStructors(TurboMac);

public:
    bool init(OSDictionary *dictionary = nullptr) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;

    IOReturn newUserClient(
        task_t owningTask,
        void *securityID,
        UInt32 type,
        OSDictionary *properties,
        IOUserClient **handler
    ) override;
    IOReturn newUserClient(
        task_t owningTask,
        void *securityID,
        UInt32 type,
        IOUserClient **handler
    ) override;

    IOReturn copyCapabilities(TurboMacCapabilities *output);
    IOReturn copyTelemetry(TurboMacTelemetry *output);
    IOReturn arm(const TurboMacLimitRequest *request);
    IOReturn update(const TurboMacLimitRequest *request);
    IOReturn disarm(const TurboMacCommandRequest *request);
    IOReturn copyStatus(TurboMacDriverStatus *output);
    IOReturn copyHWPStatus(TurboMacHWPStatus *output);
    void invalidClientCommand();
    void clientDisconnected();

private:
    static constexpr uint32_t kSupportedFamily = 6U;
    static constexpr uint32_t kSupportedModel = 0x9eU;
    static constexpr uint32_t kWatchdogTimeoutMS = 5000U;
    static constexpr uint32_t kWatchdogPollMS = 250U;
    static constexpr uint32_t kPassiveGuardPollMS = 1000U;
    static constexpr uint32_t kHWPBootstrapLimitMW = 5000U;

    static constexpr uint32_t kMSRPowerControl = 0x1fcU;
    static constexpr uint32_t kMSRRAPLPowerUnit = 0x606U;
    static constexpr uint32_t kMSRPackagePowerLimit = 0x610U;
    static constexpr uint32_t kMSRPackageEnergyStatus = 0x611U;
    static constexpr uint32_t kMSRPackagePowerInfo = 0x614U;
    static constexpr uint32_t kMSRHWPEnable = 0x770U;
    static constexpr uint32_t kMSRHWPCapabilities = 0x771U;
    static constexpr uint32_t kMSRHWPPackageRequest = 0x772U;
    static constexpr uint32_t kMSRHWPRequest = 0x774U;
    static constexpr uint64_t kEnableBidirProchot = UINT64_C(1);
    static constexpr uint64_t kHWPEnable = UINT64_C(1);
    static constexpr uint32_t kHWPFeatureSupported = 1U << 7U;
    static constexpr uint32_t kHWPFeatureEPP = 1U << 10U;
    static constexpr uint32_t kHWPFeaturePackageRequest = 1U << 11U;
    static constexpr uint32_t kHWPFeatureFlexibleRequestFields = 1U << 17U;
    static constexpr uint32_t kMaximumLogicalCPUs = TURBOMAC_MAX_LOGICAL_CPUS;

    enum PowerControlOperation : uint32_t {
        kPowerControlRead = 0U,
        kPowerControlClearGuard = 1U,
        kPowerControlSetGuard = 2U,
    };

    struct PowerControlBroadcast {
        volatile SInt32 count;
        volatile SInt32 overflow;
        uint32_t operation;
        uint32_t reserved;
        uint64_t values[kMaximumLogicalCPUs];
    };

    enum HWPRequestOperation : uint32_t {
        kHWPRequestRead = 0U,
        kHWPRequestWrite = 1U,
    };

    struct HWPRequestBroadcast {
        volatile SInt32 count;
        volatile SInt32 overflow;
        uint32_t operation;
        uint32_t reserved;
        uint64_t targets[kMaximumLogicalCPUs];
        uint64_t values[kMaximumLogicalCPUs];
        uint8_t targetPresent[kMaximumLogicalCPUs];
        uint8_t present[kMaximumLogicalCPUs];
    };

    static void watchdogFired(OSObject *owner, IOTimerEventSource *sender);
    static void powerControlOnCPU(void *argument);
    static void hwpRequestOnCPU(void *argument);

    bool initializeCapabilitiesLocked();
    bool reserveClientLocked();
    void releaseClientLocked();
    bool validateRequestLocked(const TurboMacLimitRequest *request, bool isArm) const;
    bool installLimitsLocked(uint32_t pl1MW, uint32_t pl2MW);
    bool verifyLimitsLocked(uint64_t expected) const;
    bool readPowerControlLocked(
        bool *allEnabled,
        bool *allDisabled,
        uint64_t *representative
    );
    bool ensurePassiveGuardLocked();
    bool setPowerControlGuardLocked(bool enabled);
    bool initializeHWPLocked();
    bool readHWPRequestsLocked(
        uint64_t values[kMaximumLogicalCPUs],
        uint8_t present[kMaximumLogicalCPUs],
        uint32_t *count
    ) const;
    bool writeHWPRequestsLocked(
        const uint64_t values[kMaximumLogicalCPUs],
        const uint8_t present[kMaximumLogicalCPUs]
    ) const;
    bool captureHWPStateLocked();
    bool bootstrapHWPLocked();
    bool ensureHWPFailSafeLocked();
    bool verifyHWPFailSafeLocked() const;
    bool applyHWPMaximumLocked(uint32_t mode);
    bool verifyHWPOverrideLocked() const;
    bool restoreHWPLocked();
    bool verifyArmedStateLocked();
    bool hwpMaximumRestrictedLocked(
        const uint64_t values[kMaximumLogicalCPUs],
        const uint8_t present[kMaximumLogicalCPUs],
        uint64_t packageRequest
    ) const;
    bool restoreLocked(uint32_t reason);
    void scheduleWatchdogLocked();
    void schedulePassiveGuardLocked();
    uint64_t readAbsoluteTime() const;
    bool watchdogExpiredLocked(uint64_t now) const;
    static void readCPUIdentity(uint32_t *family, uint32_t *model, uint32_t *stepping);

    IOLock *lock_;
    IOWorkLoop *workLoop_;
    IOTimerEventSource *watchdog_;
    bool clientReserved_;
    bool supported_;
    bool captured_;
    uint32_t state_;
    uint32_t restoreReason_;
    uint32_t powerExponent_;
    uint32_t energyExponent_;
    uint32_t floorMW_;
    uint32_t capabilityMaximumMW_;
    uint32_t thermalSpecMW_;
    uint32_t ceilingPL1MW_;
    uint32_t ceilingPL2MW_;
    uint32_t appliedPL1MW_;
    uint32_t appliedPL2MW_;
    uint32_t watchdogTimeoutMS_;
    uint32_t cpuFamily_;
    uint32_t cpuModel_;
    uint32_t cpuStepping_;
    uint32_t logicalCPUCount_;
    uint32_t hwpCPUIDEAX_;
    uint32_t hwpMode_;
    uint64_t unitsRaw_;
    uint64_t powerInfoRaw_;
    uint64_t capturedPackageLimit_;
    uint64_t capturedPowerControl_;
    uint64_t currentPowerControl_;
    uint64_t expectedPackageLimit_;
    uint64_t hwpPMEnableRaw_;
    uint64_t hwpCapabilitiesRaw_;
    uint64_t capturedHWPPackageRequest_;
    uint64_t expectedHWPPackageRequest_;
    uint64_t hwpFailSafeRequest_;
    uint64_t capturedHWPRequests_[kMaximumLogicalCPUs];
    uint64_t expectedHWPRequests_[kMaximumLogicalCPUs];
    uint8_t capturedHWPPresent_[kMaximumLogicalCPUs];
    bool hwpSupported_;
    bool hwpEnabled_;
    bool hwpEPPSupported_;
    bool hwpFlexibleRequestFields_;
    bool hwpPackageRequestSupported_;
    bool hwpCaptured_;
    bool hwpOverrideActive_;
    bool hwpEnabledByTurboMac_;
    bool hwpFailSafeActive_;
    uint64_t lastSequence_;
    uint64_t lastHeartbeat_;
};

#endif
