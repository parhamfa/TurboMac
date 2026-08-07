#include "TurboMacUserClient.h"
#include "TurboMac.h"

#include <IOKit/IOLib.h>

#include "../Shared/TurboMacProtocol.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(TurboMacUserClient, IOUserClient);

bool TurboMacUserClient::start(IOService *provider) {
    driver_ = OSDynamicCast(TurboMac, provider);
    closed_ = false;
    if (driver_ == nullptr || !super::start(provider)) {
        driver_ = nullptr;
        return false;
    }
    driver_->retain();
    return true;
}

void TurboMacUserClient::stop(IOService *provider) {
    closeOnce();
    super::stop(provider);
}

void TurboMacUserClient::free() {
    closeOnce();
    if (driver_ != nullptr) {
        driver_->release();
        driver_ = nullptr;
    }
    super::free();
}

IOReturn TurboMacUserClient::clientClose() {
    closeOnce();
    terminate();
    return kIOReturnSuccess;
}

IOReturn TurboMacUserClient::clientDied() {
    return clientClose();
}

IOReturn TurboMacUserClient::externalMethod(
    uint32_t selector,
    IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *dispatch,
    OSObject *target,
    void *reference
) {
    (void)dispatch;
    (void)target;
    (void)reference;
    if (closed_ || driver_ == nullptr || arguments == nullptr) {
        return kIOReturnNotOpen;
    }
    if (arguments->scalarInputCount != 0U || arguments->scalarOutputCount != 0U) {
        return rejectInvalid();
    }

    switch (selector) {
        case kTurboMacSelectorGetCapabilities: {
            if (!validateOutput(arguments, sizeof(TurboMacCapabilities))) {
                return rejectInvalid();
            }
            TurboMacCapabilities output = {};
            const IOReturn result = driver_->copyCapabilities(&output);
            if (result == kIOReturnSuccess) {
                bcopy(&output, arguments->structureOutput, sizeof(output));
                arguments->structureOutputSize = sizeof(output);
            }
            return result;
        }
        case kTurboMacSelectorReadTelemetry: {
            if (!validateOutput(arguments, sizeof(TurboMacTelemetry))) {
                return rejectInvalid();
            }
            TurboMacTelemetry output = {};
            const IOReturn result = driver_->copyTelemetry(&output);
            if (result == kIOReturnSuccess) {
                bcopy(&output, arguments->structureOutput, sizeof(output));
                arguments->structureOutputSize = sizeof(output);
            }
            return result;
        }
        case kTurboMacSelectorArm:
        case kTurboMacSelectorUpdate: {
            if (!validateInput(arguments, sizeof(TurboMacLimitRequest))
                || arguments->structureOutput != nullptr
                || arguments->structureOutputSize != 0U) {
                return rejectInvalid();
            }
            TurboMacLimitRequest request = {};
            bcopy(arguments->structureInput, &request, sizeof(request));
            return selector == kTurboMacSelectorArm
                ? driver_->arm(&request)
                : driver_->update(&request);
        }
        case kTurboMacSelectorDisarm: {
            if (!validateInput(arguments, sizeof(TurboMacCommandRequest))
                || arguments->structureOutput != nullptr
                || arguments->structureOutputSize != 0U) {
                return rejectInvalid();
            }
            TurboMacCommandRequest request = {};
            bcopy(arguments->structureInput, &request, sizeof(request));
            return driver_->disarm(&request);
        }
        case kTurboMacSelectorGetStatus: {
            if (!validateOutput(arguments, sizeof(TurboMacDriverStatus))) {
                return rejectInvalid();
            }
            TurboMacDriverStatus output = {};
            const IOReturn result = driver_->copyStatus(&output);
            if (result == kIOReturnSuccess) {
                bcopy(&output, arguments->structureOutput, sizeof(output));
                arguments->structureOutputSize = sizeof(output);
            }
            return result;
        }
        default:
            return rejectInvalid();
    }
}

IOReturn TurboMacUserClient::closeOnce() {
    if (closed_) {
        return kIOReturnSuccess;
    }
    closed_ = true;
    if (driver_ != nullptr) {
        driver_->clientDisconnected();
    }
    return kIOReturnSuccess;
}

IOReturn TurboMacUserClient::rejectInvalid() {
    if (driver_ != nullptr) {
        driver_->invalidClientCommand();
    }
    return kIOReturnBadArgument;
}

bool TurboMacUserClient::validateOutput(IOExternalMethodArguments *arguments, size_t size) const {
    return arguments->structureInput == nullptr
        && arguments->structureInputSize == 0U
        && arguments->structureOutput != nullptr
        && arguments->structureOutputSize >= size;
}

bool TurboMacUserClient::validateInput(IOExternalMethodArguments *arguments, size_t size) const {
    return arguments->structureInput != nullptr
        && arguments->structureInputSize == size;
}
