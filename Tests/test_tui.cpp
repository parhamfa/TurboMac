#include "TurboMacTopCore.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {

const char *kStatus = R"JSON({
  "ok":true,
  "armed":true,
  "profile_valid":true,
  "auto_arm":true,
  "validation_passed":true,
  "telemetry_ready":true,
  "band":"cruise",
  "input_w":31.25,
  "capacity_w":79.496,
  "package_w":20.5,
  "non_cpu_w":10.75,
  "guard_w":45.003,
  "shed_w":51.998,
  "emergency_w":58.0,
  "pl1_w":24.0,
  "pl2_w":34.0,
  "cpu_temp_c":72.5,
  "non_cpu_over_budget":false,
  "smc_invalid_consecutive":0,
  "apple_guard_enabled":false,
  "rapl_locked":false,
  "hwp_enabled":true,
  "hwp_failsafe_active":false,
  "hwp_readback_valid":true,
  "hwp_logical_cpu_count":8
})JSON";

void testStatusParser() {
    turbomactop::Snapshot status;
    std::string error;
    assert(turbomactop::parseStatusJSON(kStatus, &status, &error));
    assert(status.valid);
    assert(status.armed);
    assert(status.band == "cruise");
    assert(std::fabs(status.inputW - 31.25) < 0.001);
    assert(std::fabs(status.pl2W - 34.0) < 0.001);
    assert(status.hwpLogicalCPUCount == 8U);

    std::string malformed = kStatus;
    const std::size_t field = malformed.find("\"pl2_w\":34.0,");
    assert(field != std::string::npos);
    malformed.erase(field, std::string("\"pl2_w\":34.0,").size());
    assert(!turbomactop::parseStatusJSON(malformed, &status, &error));
}

void testFrequencyParser() {
    double mhz = 0.0;
    double percent = 0.0;
    assert(turbomactop::parsePowermetricsFrequency(
        "System Average frequency as fraction of nominal: 71.29% (2067.53 Mhz)",
        &mhz,
        &percent
    ));
    assert(std::fabs(mhz - 2067.53) < 0.001);
    assert(std::fabs(percent - 71.29) < 0.001);
    assert(!turbomactop::parsePowermetricsFrequency("CPU Average frequency", &mhz, &percent));
}

void expectControl(std::string input, turbomactop::ControlAction expected) {
    turbomactop::ControlAction action = turbomactop::ControlAction::None;
    assert(turbomactop::consumeControlInput(&input, &action));
    assert(action == expected);
    assert(input.empty());
}

void testControlParser() {
    expectControl("\x1b[A", turbomactop::ControlAction::DutyUp);
    expectControl("\x1bOB", turbomactop::ControlAction::DutyDown);
    expectControl("\x1b[1;5C", turbomactop::ControlAction::WorkersUp);
    expectControl("\xe2\x86\x90", turbomactop::ControlAction::WorkersDown);
    expectControl("w", turbomactop::ControlAction::DutyUp);
    expectControl("S", turbomactop::ControlAction::DutyDown);
    expectControl("]", turbomactop::ControlAction::WorkersUp);
    expectControl(" ", turbomactop::ControlAction::ToggleLoad);
    expectControl("\r", turbomactop::ControlAction::ToggleLoad);
    expectControl("x", turbomactop::ControlAction::StopLoad);
    expectControl("M", turbomactop::ControlAction::MaximumPreset);
    expectControl("q", turbomactop::ControlAction::Quit);

    std::string partial = "\x1b[1;";
    turbomactop::ControlAction action = turbomactop::ControlAction::None;
    assert(!turbomactop::consumeControlInput(&partial, &action));
    partial += "2A";
    assert(turbomactop::consumeControlInput(&partial, &action));
    assert(action == turbomactop::ControlAction::DutyUp);
    assert(partial.empty());
}

void testWarningsAreVisualOnly() {
    turbomactop::Snapshot status;
    std::string error;
    assert(turbomactop::parseStatusJSON(kStatus, &status, &error));
    auto warning = turbomactop::assessWarnings(status, false, false, false);
    assert(warning.level == turbomactop::WarningLevel::Normal);

    status.band = "guard";
    status.inputW = status.guardW;
    warning = turbomactop::assessWarnings(status, false, false, true);
    assert(warning.level == turbomactop::WarningLevel::Caution);

    status.band = "emergency";
    status.inputW = status.emergencyW;
    status.cpuTempC = 96.0;
    warning = turbomactop::assessWarnings(status, false, false, true);
    assert(warning.level == turbomactop::WarningLevel::Warning);

    status.valid = false;
    warning = turbomactop::assessWarnings(status, true, true, true);
    assert(warning.level == turbomactop::WarningLevel::Warning);
    bool continuedWarning = false;
    for (const std::string &message : warning.messages) {
        if (message == "LOAD CONTINUES WITHOUT TELEMETRY") {
            continuedWarning = true;
        }
    }
    assert(continuedWarning);
}

void testSparkline() {
    const std::string line = turbomactop::sparkline({0.0, 25.0, 50.0, 75.0}, 4U, 100.0);
    assert(line.size() == 4U);
    assert(line.front() == ' ');
    assert(line.back() != ' ');
}

} // namespace

int main() {
    testStatusParser();
    testFrequencyParser();
    testControlParser();
    testWarningsAreVisualOnly();
    testSparkline();
    std::cout << "turbomactop tests passed\n";
    return 0;
}
