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
    IOReturn closeOnce();
    IOReturn rejectInvalid();
    bool validateOutput(IOExternalMethodArguments *arguments, size_t size) const;
    bool validateInput(IOExternalMethodArguments *arguments, size_t size) const;

    TurboMac *driver_;
    bool closed_;
};

#endif
