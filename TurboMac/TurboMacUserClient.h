#ifndef TURBOMAC_USER_CLIENT_H
#define TURBOMAC_USER_CLIENT_H

#include <IOKit/IOUserClient.h>

class TurboMac;

class TurboMacUserClient : public IOUserClient {
    OSDeclareDefaultStructors(TurboMacUserClient);

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    IOReturn clientClose() override;
    IOReturn clientDied() override;
    IOReturn externalMethod(
        uint32_t selector,
        IOExternalMethodArguments *arguments,
        IOExternalMethodDispatch *dispatch = nullptr,
        OSObject *target = nullptr,
        void *reference = nullptr
    ) override;

private:
    static IOReturn getCapabilities(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );
    static IOReturn readTelemetry(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );
    static IOReturn arm(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );
    static IOReturn update(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );
    static IOReturn disarm(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );
    static IOReturn getStatus(
        OSObject *target,
        void *reference,
        IOExternalMethodArguments *arguments
    );

    IOReturn closeOnce();
    IOReturn rejectInvalid();

    static IOExternalMethodDispatch dispatchTable_[];

    TurboMac *driver_;
    bool closed_;
};

#endif
