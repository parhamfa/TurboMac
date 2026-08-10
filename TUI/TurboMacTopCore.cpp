#include "TurboMacTopCore.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace turbomactop {
namespace {

bool findValue(const std::string &json, const char *key, std::size_t *position) {
    const std::string token = std::string("\"") + key + "\"";
    std::size_t found = json.find(token);
    if (found == std::string::npos) {
        return false;
    }
    found = json.find(':', found + token.size());
    if (found == std::string::npos) {
        return false;
    }
    found++;
    while (found < json.size()
           && (json[found] == ' ' || json[found] == '\t'
               || json[found] == '\r' || json[found] == '\n')) {
        found++;
    }
    *position = found;
    return found < json.size();
}

bool readBool(const std::string &json, const char *key, bool *value) {
    std::size_t position = 0U;
    if (!findValue(json, key, &position)) {
        return false;
    }
    if (json.compare(position, 4U, "true") == 0) {
        *value = true;
        return true;
    }
    if (json.compare(position, 5U, "false") == 0) {
        *value = false;
        return true;
    }
    return false;
}

bool readDouble(const std::string &json, const char *key, double *value) {
    std::size_t position = 0U;
    if (!findValue(json, key, &position)) {
        return false;
    }
    errno = 0;
    char *end = nullptr;
    const double parsed = std::strtod(json.c_str() + position, &end);
    if (errno != 0 || end == json.c_str() + position || !std::isfinite(parsed)) {
        return false;
    }
    *value = parsed;
    return true;
}

bool readUnsigned(const std::string &json, const char *key, unsigned *value) {
    double parsed = 0.0;
    if (!readDouble(json, key, &parsed)
        || parsed < 0.0
        || parsed > 4294967295.0
        || std::floor(parsed) != parsed) {
        return false;
    }
    *value = static_cast<unsigned>(parsed);
    return true;
}

bool readString(const std::string &json, const char *key, std::string *value) {
    std::size_t position = 0U;
    if (!findValue(json, key, &position)
        || position >= json.size()
        || json[position] != '"') {
        return false;
    }
    const std::size_t end = json.find('"', position + 1U);
    if (end == std::string::npos) {
        return false;
    }
    *value = json.substr(position + 1U, end - position - 1U);
    return true;
}

void raise(
    WarningState *state,
    WarningLevel level,
    const std::string &message
) {
    if (static_cast<int>(level) > static_cast<int>(state->level)) {
        state->level = level;
    }
    state->messages.push_back(message);
}

} // namespace

bool parseStatusJSON(const std::string &json, Snapshot *snapshot, std::string *error) {
    if (snapshot == nullptr) {
        if (error != nullptr) {
            *error = "null snapshot";
        }
        return false;
    }

    Snapshot parsed;
    const bool valid = readBool(json, "ok", &parsed.ok)
        && readBool(json, "armed", &parsed.armed)
        && readBool(json, "profile_valid", &parsed.profileValid)
        && readBool(json, "auto_arm", &parsed.autoArm)
        && readBool(json, "validation_passed", &parsed.validationPassed)
        && readBool(json, "telemetry_ready", &parsed.telemetryReady)
        && readString(json, "band", &parsed.band)
        && readDouble(json, "input_w", &parsed.inputW)
        && readDouble(json, "capacity_w", &parsed.capacityW)
        && readDouble(json, "package_w", &parsed.packageW)
        && readDouble(json, "non_cpu_w", &parsed.nonCPUW)
        && readDouble(json, "guard_w", &parsed.guardW)
        && readDouble(json, "shed_w", &parsed.shedW)
        && readDouble(json, "emergency_w", &parsed.emergencyW)
        && readDouble(json, "pl1_w", &parsed.pl1W)
        && readDouble(json, "pl2_w", &parsed.pl2W)
        && readDouble(json, "cpu_temp_c", &parsed.cpuTempC)
        && readBool(json, "non_cpu_over_budget", &parsed.nonCPUOverBudget)
        && readUnsigned(json, "smc_invalid_consecutive", &parsed.smcInvalidConsecutive)
        && readBool(json, "apple_guard_enabled", &parsed.appleGuardEnabled)
        && readBool(json, "rapl_locked", &parsed.raplLocked)
        && readBool(json, "hwp_enabled", &parsed.hwpEnabled)
        && readBool(json, "hwp_failsafe_active", &parsed.hwpFailsafeActive)
        && readBool(json, "hwp_readback_valid", &parsed.hwpReadbackValid)
        && readUnsigned(json, "hwp_logical_cpu_count", &parsed.hwpLogicalCPUCount);
    if (!valid) {
        if (error != nullptr) {
            *error = "missing or malformed status field";
        }
        return false;
    }
    if (parsed.band != "cruise"
        && parsed.band != "guard"
        && parsed.band != "shed"
        && parsed.band != "emergency") {
        if (error != nullptr) {
            *error = "unknown governor band";
        }
        return false;
    }
    parsed.valid = true;
    *snapshot = parsed;
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool parsePowermetricsFrequency(const std::string &line, double *mhz, double *percent) {
    if (mhz == nullptr || percent == nullptr) {
        return false;
    }
    constexpr const char *prefix = "System Average frequency as fraction of nominal:";
    const std::size_t position = line.find(prefix);
    if (position == std::string::npos) {
        return false;
    }
    const char *cursor = line.c_str() + position + std::char_traits<char>::length(prefix);
    errno = 0;
    char *end = nullptr;
    const double parsedPercent = std::strtod(cursor, &end);
    if (errno != 0 || end == cursor || !std::isfinite(parsedPercent)) {
        return false;
    }
    const char *open = std::strchr(end, '(');
    if (open == nullptr) {
        return false;
    }
    errno = 0;
    const double parsedMHz = std::strtod(open + 1, &end);
    if (errno != 0 || end == open + 1 || !std::isfinite(parsedMHz)) {
        return false;
    }
    *mhz = parsedMHz;
    *percent = parsedPercent;
    return true;
}

bool consumeControlInput(std::string *input, ControlAction *action) {
    if (input == nullptr || action == nullptr || input->empty()) {
        return false;
    }

    *action = ControlAction::None;
    const unsigned char first = static_cast<unsigned char>((*input)[0]);
    if (first == 0x1bU) {
        if (input->size() < 2U) {
            return false;
        }
        if ((*input)[1] == '[') {
            std::size_t final = 2U;
            while (final < input->size()) {
                const unsigned char character = static_cast<unsigned char>((*input)[final]);
                if (character >= 0x40U && character <= 0x7eU) {
                    break;
                }
                final++;
            }
            if (final == input->size()) {
                return false;
            }
            const char direction = (*input)[final];
            *action = direction == 'A' ? ControlAction::DutyUp
                : direction == 'B' ? ControlAction::DutyDown
                : direction == 'C' ? ControlAction::WorkersUp
                : direction == 'D' ? ControlAction::WorkersDown
                : ControlAction::None;
            input->erase(0U, final + 1U);
            return true;
        }
        if ((*input)[1] == 'O') {
            if (input->size() < 3U) {
                return false;
            }
            const char direction = (*input)[2];
            *action = direction == 'A' ? ControlAction::DutyUp
                : direction == 'B' ? ControlAction::DutyDown
                : direction == 'C' ? ControlAction::WorkersUp
                : direction == 'D' ? ControlAction::WorkersDown
                : ControlAction::None;
            input->erase(0U, 3U);
            return true;
        }
        input->erase(0U, 1U);
        return true;
    }

    if (first == 0xe2U) {
        if (input->size() < 3U) {
            return false;
        }
        const unsigned char second = static_cast<unsigned char>((*input)[1]);
        const unsigned char third = static_cast<unsigned char>((*input)[2]);
        if (second == 0x86U) {
            *action = third == 0x91U ? ControlAction::DutyUp
                : third == 0x93U ? ControlAction::DutyDown
                : third == 0x92U ? ControlAction::WorkersUp
                : third == 0x90U ? ControlAction::WorkersDown
                : ControlAction::None;
        }
        input->erase(0U, 3U);
        return true;
    }

    input->erase(0U, 1U);
    switch (first) {
        case 'w':
        case 'W':
        case '+':
        case '=':
            *action = ControlAction::DutyUp;
            break;
        case 's':
        case 'S':
        case '-':
        case '_':
            *action = ControlAction::DutyDown;
            break;
        case 'd':
        case 'D':
        case ']':
            *action = ControlAction::WorkersUp;
            break;
        case 'a':
        case 'A':
        case '[':
            *action = ControlAction::WorkersDown;
            break;
        case ' ':
        case '\r':
        case '\n':
            *action = ControlAction::ToggleLoad;
            break;
        case '0':
        case 'x':
        case 'X':
            *action = ControlAction::StopLoad;
            break;
        case 'm':
        case 'M':
            *action = ControlAction::MaximumPreset;
            break;
        case 'r':
        case 'R':
            *action = ControlAction::ResetPeaks;
            break;
        case 'q':
        case 'Q':
            *action = ControlAction::Quit;
            break;
        default:
            break;
    }
    return true;
}

const char *controlActionName(ControlAction action) {
    switch (action) {
        case ControlAction::DutyUp:
            return "DUTY +5%";
        case ControlAction::DutyDown:
            return "DUTY -5%";
        case ControlAction::WorkersUp:
            return "WORKERS +1";
        case ControlAction::WorkersDown:
            return "WORKERS -1";
        case ControlAction::ToggleLoad:
            return "LOAD TOGGLE";
        case ControlAction::StopLoad:
            return "LOAD IDLE";
        case ControlAction::MaximumPreset:
            return "MAX PRESET";
        case ControlAction::ResetPeaks:
            return "PEAKS RESET";
        case ControlAction::Quit:
            return "QUIT";
        case ControlAction::None:
            return "IGNORED KEY";
    }
    return "UNKNOWN";
}

WarningState assessWarnings(
    const Snapshot &snapshot,
    bool statusStale,
    bool frequencyStale,
    bool loadRunning
) {
    WarningState state;
    if (!snapshot.valid || statusStale) {
        raise(&state, WarningLevel::Warning, "TURBOMAC DATA STALE");
        if (loadRunning) {
            raise(&state, WarningLevel::Warning, "LOAD CONTINUES WITHOUT TELEMETRY");
        }
        return state;
    }
    if (!snapshot.ok || !snapshot.telemetryReady) {
        raise(&state, WarningLevel::Warning, "POWER TELEMETRY INVALID");
    }
    if (!snapshot.armed) {
        raise(
            &state,
            loadRunning ? WarningLevel::Warning : WarningLevel::Caution,
            loadRunning ? "LOAD ACTIVE / GOVERNOR DISARMED" : "GOVERNOR DISARMED"
        );
    }
    if (snapshot.band == "emergency") {
        raise(&state, WarningLevel::Warning, "EMERGENCY POWER BAND");
    } else if (snapshot.band == "shed") {
        raise(&state, WarningLevel::Warning, "SHED POWER BAND");
    } else if (snapshot.band == "guard") {
        raise(&state, WarningLevel::Caution, "GUARD POWER BAND");
    }
    if (snapshot.inputW >= snapshot.emergencyW && snapshot.emergencyW > 0.0) {
        raise(&state, WarningLevel::Warning, "INPUT ABOVE EMERGENCY THRESHOLD");
    } else if (snapshot.inputW >= snapshot.shedW && snapshot.shedW > 0.0) {
        raise(&state, WarningLevel::Warning, "INPUT ABOVE SHED THRESHOLD");
    } else if (snapshot.inputW >= snapshot.guardW && snapshot.guardW > 0.0) {
        raise(&state, WarningLevel::Caution, "INPUT ABOVE GUARD THRESHOLD");
    }
    if (snapshot.cpuTempC >= 95.0) {
        raise(&state, WarningLevel::Warning, "CPU TEMPERATURE 95 C OR HIGHER");
    } else if (snapshot.cpuTempC >= 85.0) {
        raise(&state, WarningLevel::Caution, "CPU TEMPERATURE HIGH");
    }
    if (snapshot.nonCPUOverBudget) {
        raise(&state, WarningLevel::Warning, "NON-CPU LOAD OVER BUDGET");
    }
    if (snapshot.raplLocked) {
        raise(&state, WarningLevel::Warning, "RAPL LIMIT REGISTER LOCKED");
    }
    if (!snapshot.hwpReadbackValid) {
        raise(&state, WarningLevel::Warning, "HWP READBACK INVALID");
    }
    if (snapshot.armed && snapshot.appleGuardEnabled) {
        raise(&state, WarningLevel::Caution, "APPLE GUARD ACTIVE WHILE ARMED");
    }
    if (snapshot.smcInvalidConsecutive > 0U) {
        raise(&state, WarningLevel::Caution, "SMC SAMPLE ERRORS");
    }
    if (frequencyStale) {
        raise(&state, WarningLevel::Caution, "CPU FREQUENCY UNAVAILABLE");
    }
    if (state.messages.empty()) {
        state.messages.push_back("ALL MONITORED SYSTEMS NORMAL");
    }
    return state;
}

std::string sparkline(const std::vector<double> &values, std::size_t width, double ceiling) {
    static constexpr const char *levels = " .:-=+*#%@";
    static constexpr std::size_t levelCount = 10U;
    if (width == 0U || values.empty() || !std::isfinite(ceiling) || ceiling <= 0.0) {
        return std::string(width, ' ');
    }
    std::string result;
    result.reserve(width);
    for (std::size_t column = 0U; column < width; ++column) {
        const std::size_t begin = column * values.size() / width;
        std::size_t end = (column + 1U) * values.size() / width;
        if (end <= begin) {
            end = std::min(values.size(), begin + 1U);
        }
        double peak = 0.0;
        for (std::size_t index = begin; index < end && index < values.size(); ++index) {
            if (std::isfinite(values[index])) {
                peak = std::max(peak, values[index]);
            }
        }
        const double ratio = std::max(0.0, std::min(1.0, peak / ceiling));
        const std::size_t level = static_cast<std::size_t>(
            std::round(ratio * static_cast<double>(levelCount - 1U))
        );
        result.push_back(levels[level]);
    }
    return result;
}

} // namespace turbomactop
