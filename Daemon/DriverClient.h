#ifndef TURBOMAC_DRIVER_CLIENT_H
#define TURBOMAC_DRIVER_CLIENT_H

#include <IOKit/IOKitLib.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../Shared/TurboMacProtocol.h"

class DriverClient {
public:
    DriverClient();
    ~DriverClient();
    DriverClient(const DriverClient &) = delete;
    DriverClient &operator=(const DriverClient &) = delete;

    bool open(std::string *error);
    void close();
    bool isOpen() const;
    bool capabilities(TurboMacCapabilities *output, std::string *error) const;
    bool telemetry(TurboMacTelemetry *output, std::string *error) const;
    bool status(TurboMacDriverStatus *output, std::string *error) const;
    bool hwpStatus(TurboMacHWPStatus *output, std::string *error) const;
    bool arm(
        uint32_t pl1MW,
        uint32_t pl2MW,
        uint32_t hwpMode,
        std::string *error
    );
    bool update(uint32_t pl1MW, uint32_t pl2MW, std::string *error);
    bool disarm(std::string *error);

private:
    bool callOutput(uint32_t selector, void *output, size_t size, std::string *error) const;
    bool callInput(uint32_t selector, const void *input, size_t size, std::string *error) const;
    uint64_t nextSequence();
    static void setError(std::string *error, const char *operation, kern_return_t result);

    io_connect_t connection_;
    uint64_t sequence_;
};

#endif
