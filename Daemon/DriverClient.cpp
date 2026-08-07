#include "DriverClient.h"

#include <mach/mach_time.h>
#include <sstream>

DriverClient::DriverClient()
    : connection_(IO_OBJECT_NULL), sequence_(mach_absolute_time()) {}

DriverClient::~DriverClient() {
    close();
}

bool DriverClient::open(std::string *error) {
    close();
    io_service_t service = IOServiceGetMatchingService(
        kIOMainPortDefault,
        IOServiceMatching(TURBOMAC_SERVICE_NAME)
    );
    if (service == IO_OBJECT_NULL) {
        if (error != nullptr) {
            *error = "TurboMac IOService was not found";
        }
        return false;
    }
    const kern_return_t result = IOServiceOpen(service, mach_task_self(), 0, &connection_);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS) {
        connection_ = IO_OBJECT_NULL;
        setError(error, "IOServiceOpen(TurboMac)", result);
        return false;
    }
    return true;
}

void DriverClient::close() {
    if (connection_ != IO_OBJECT_NULL) {
        IOServiceClose(connection_);
        connection_ = IO_OBJECT_NULL;
    }
}

bool DriverClient::isOpen() const {
    return connection_ != IO_OBJECT_NULL;
}

bool DriverClient::capabilities(TurboMacCapabilities *output, std::string *error) const {
    return callOutput(kTurboMacSelectorGetCapabilities, output, sizeof(*output), error);
}

bool DriverClient::telemetry(TurboMacTelemetry *output, std::string *error) const {
    return callOutput(kTurboMacSelectorReadTelemetry, output, sizeof(*output), error);
}

bool DriverClient::status(TurboMacDriverStatus *output, std::string *error) const {
    return callOutput(kTurboMacSelectorGetStatus, output, sizeof(*output), error);
}

bool DriverClient::arm(uint32_t pl1MW, uint32_t pl2MW, std::string *error) {
    TurboMacLimitRequest request = {};
    request.version = TURBOMAC_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.pl1_mw = pl1MW;
    request.pl2_mw = pl2MW;
    request.watchdog_timeout_ms = 5000U;
    request.sequence = nextSequence();
    return callInput(kTurboMacSelectorArm, &request, sizeof(request), error);
}

bool DriverClient::update(uint32_t pl1MW, uint32_t pl2MW, std::string *error) {
    TurboMacLimitRequest request = {};
    request.version = TURBOMAC_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.pl1_mw = pl1MW;
    request.pl2_mw = pl2MW;
    request.watchdog_timeout_ms = 5000U;
    request.sequence = nextSequence();
    return callInput(kTurboMacSelectorUpdate, &request, sizeof(request), error);
}

bool DriverClient::disarm(std::string *error) {
    TurboMacCommandRequest request = {};
    request.version = TURBOMAC_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.sequence = nextSequence();
    return callInput(kTurboMacSelectorDisarm, &request, sizeof(request), error);
}

bool DriverClient::callOutput(
    uint32_t selector,
    void *output,
    size_t size,
    std::string *error
) const {
    if (connection_ == IO_OBJECT_NULL || output == nullptr) {
        if (error != nullptr) {
            *error = "TurboMac driver is not open";
        }
        return false;
    }
    size_t outputSize = size;
    const kern_return_t result = IOConnectCallStructMethod(
        connection_, selector, nullptr, 0, output, &outputSize
    );
    if (result != KERN_SUCCESS) {
        setError(error, "TurboMac driver call", result);
        return false;
    }
    if (outputSize != size) {
        if (error != nullptr) {
            *error = "TurboMac driver returned an unexpected payload size";
        }
        return false;
    }
    return true;
}

bool DriverClient::callInput(
    uint32_t selector,
    const void *input,
    size_t size,
    std::string *error
) const {
    if (connection_ == IO_OBJECT_NULL || input == nullptr) {
        if (error != nullptr) {
            *error = "TurboMac driver is not open";
        }
        return false;
    }
    const kern_return_t result = IOConnectCallStructMethod(
        connection_, selector, input, size, nullptr, nullptr
    );
    if (result != KERN_SUCCESS) {
        setError(error, "TurboMac driver call", result);
        return false;
    }
    return true;
}

uint64_t DriverClient::nextSequence() {
    const uint64_t now = mach_absolute_time();
    sequence_ = now > sequence_ ? now : sequence_ + 1U;
    return sequence_;
}

void DriverClient::setError(std::string *error, const char *operation, kern_return_t result) {
    if (error == nullptr) {
        return;
    }
    std::ostringstream stream;
    stream << operation << " failed: 0x" << std::hex << result;
    *error = stream.str();
}
