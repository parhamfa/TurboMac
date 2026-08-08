#include "TurboMacUserClient.h"
#include "TurboMac.h"

#include <IOKit/IOLib.h>

#include "../Shared/TurboMacProtocol.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(TurboMacUserClient, IOUserClient);

IOExternalMethodDispatch TurboMacUserClient::dispatchTable_[kTurboMacSelectorCount] = {
    {
        &TurboMacUserClient::getCapabilities,
        0U,
        0U,
        0U,
        sizeof(TurboMacCapabilities),
    },
    {
        &TurboMacUserClient::readTelemetry,
        0U,
        0U,
        0U,
        sizeof(TurboMacTelemetry),
    },
    {
        &TurboMacUserClient::arm,
        0U,
        sizeof(TurboMacLimitRequest),
        0U,
        0U,
    },
    {
        &TurboMacUserClient::update,
        0U,
        sizeof(TurboMacLimitRequest),
        0U,
        0U,
    },
    {
        &TurboMacUserClient::disarm,
        0U,
        sizeof(TurboMacCommandRequest),
        0U,
        0U,
    },
    {
        &TurboMacUserClient::getStatus,
        0U,
        0U,
        0U,
        sizeof(TurboMacDriverStatus),
    },
};

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
    if (selector >= kTurboMacSelectorCount) {
        return rejectInvalid();
    }

    const IOReturn result = super::externalMethod(
        selector,
        arguments,
        &dispatchTable_[selector],
        this,
        nullptr
    );
    if (result == kIOReturnBadArgument) {
        return rejectInvalid();
    }
    return result;
}

IOReturn TurboMacUserClient::getCapabilities(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureOutput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacCapabilities output = {};
    const IOReturn result = client->driver_->copyCapabilities(&output);
    if (result == kIOReturnSuccess) {
        bcopy(&output, arguments->structureOutput, sizeof(output));
        arguments->structureOutputSize = sizeof(output);
    }
    return result;
}

IOReturn TurboMacUserClient::readTelemetry(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureOutput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacTelemetry output = {};
    const IOReturn result = client->driver_->copyTelemetry(&output);
    if (result == kIOReturnSuccess) {
        bcopy(&output, arguments->structureOutput, sizeof(output));
        arguments->structureOutputSize = sizeof(output);
    }
    return result;
}

IOReturn TurboMacUserClient::arm(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureInput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacLimitRequest request = {};
    bcopy(arguments->structureInput, &request, sizeof(request));
    return client->driver_->arm(&request);
}

IOReturn TurboMacUserClient::update(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureInput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacLimitRequest request = {};
    bcopy(arguments->structureInput, &request, sizeof(request));
    return client->driver_->update(&request);
}

IOReturn TurboMacUserClient::disarm(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureInput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacCommandRequest request = {};
    bcopy(arguments->structureInput, &request, sizeof(request));
    return client->driver_->disarm(&request);
}

IOReturn TurboMacUserClient::getStatus(
    OSObject *target,
    void *reference,
    IOExternalMethodArguments *arguments
) {
    (void)reference;
    TurboMacUserClient *client = OSDynamicCast(TurboMacUserClient, target);
    if (client == nullptr || client->driver_ == nullptr
        || arguments == nullptr || arguments->structureOutput == nullptr) {
        return kIOReturnBadArgument;
    }
    TurboMacDriverStatus output = {};
    const IOReturn result = client->driver_->copyStatus(&output);
    if (result == kIOReturnSuccess) {
        bcopy(&output, arguments->structureOutput, sizeof(output));
        arguments->structureOutputSize = sizeof(output);
    }
    return result;
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
