#ifndef TURBOMAC_TOP_CORE_H
#define TURBOMAC_TOP_CORE_H

#include <cstddef>
#include <string>
#include <vector>

namespace turbomactop {

struct Snapshot {
    bool valid = false;
    bool ok = false;
    bool armed = false;
    bool profileValid = false;
    bool autoArm = false;
    bool validationPassed = false;
    bool telemetryReady = false;
    bool nonCPUOverBudget = false;
    bool appleGuardEnabled = false;
    bool raplLocked = false;
    bool hwpEnabled = false;
    bool hwpReadbackValid = false;
    bool hwpFailsafeActive = false;
    std::string band = "unknown";
    double inputW = 0.0;
    double capacityW = 0.0;
    double packageW = 0.0;
    double nonCPUW = 0.0;
    double guardW = 0.0;
    double shedW = 0.0;
    double emergencyW = 0.0;
    double pl1W = 0.0;
    double pl2W = 0.0;
    double cpuTempC = 0.0;
    unsigned smcInvalidConsecutive = 0U;
    unsigned hwpLogicalCPUCount = 0U;
};

enum class WarningLevel {
    Normal = 0,
    Caution = 1,
    Warning = 2,
};

enum class ControlAction {
    None = 0,
    DutyUp,
    DutyDown,
    WorkersUp,
    WorkersDown,
    ToggleLoad,
    StopLoad,
    MaximumPreset,
    ResetPeaks,
    Quit,
};

struct WarningState {
    WarningLevel level = WarningLevel::Normal;
    std::vector<std::string> messages;
};

bool parseStatusJSON(const std::string &json, Snapshot *snapshot, std::string *error);
bool parsePowermetricsFrequency(const std::string &line, double *mhz, double *percent);
bool consumeControlInput(std::string *input, ControlAction *action);
const char *controlActionName(ControlAction action);
WarningState assessWarnings(
    const Snapshot &snapshot,
    bool statusStale,
    bool frequencyStale,
    bool loadRunning
);
std::string sparkline(const std::vector<double> &values, std::size_t width, double ceiling);

} // namespace turbomactop

#endif
