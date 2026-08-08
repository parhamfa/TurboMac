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
    void invalidClientCommand();
    void clientDisconnected();

private:
    static constexpr uint32_t kSupportedFamily = 6U;
    static constexpr uint32_t kSupportedModel = 0x9eU;
    static constexpr uint32_t kWatchdogTimeoutMS = 5000U;
    static constexpr uint32_t kWatchdogPollMS = 250U;
    static constexpr uint32_t kPassiveGuardPollMS = 1000U;

    static constexpr uint32_t kMSRPowerControl = 0x1fcU;
    static constexpr uint32_t kMSRRAPLPowerUnit = 0x606U;
    static constexpr uint32_t kMSRPackagePowerLimit = 0x610U;
    static constexpr uint32_t kMSRPackageEnergyStatus = 0x611U;
    static constexpr uint32_t kMSRPackagePowerInfo = 0x614U;
    static constexpr uint64_t kEnableBidirProchot = UINT64_C(1);
    static constexpr uint32_t kMaximumLogicalCPUs = 64U;

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

    static void watchdogFired(OSObject *owner, IOTimerEventSource *sender);
    static void powerControlOnCPU(void *argument);

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
    uint64_t unitsRaw_;
    uint64_t powerInfoRaw_;
    uint64_t capturedPackageLimit_;
    uint64_t capturedPowerControl_;
    uint64_t currentPowerControl_;
    uint64_t lastSequence_;
    uint64_t lastHeartbeat_;
};

#endif
