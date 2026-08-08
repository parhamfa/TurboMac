#!/usr/bin/env python3
import plistlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = (root / "TurboMac" / "TurboMac.cpp").read_text()
client = (root / "TurboMac" / "TurboMacUserClient.cpp").read_text()
daemon = (root / "Daemon" / "main.cpp").read_text()
cli = (root / "CLI" / "main.cpp").read_text()
validation = (root / "Deploy" / "whisper-validation.example").read_text()
installer = (root / "Deploy" / "install-passive.sh").read_text()
finalizer = (root / "Deploy" / "finalize-passive.sh").read_text()
with (root / "TurboMac" / "Info.plist").open("rb") as plist_file:
    driver_info = plistlib.load(plist_file)

for forbidden in (
    "IA32_MISC_ENABLE",
    "MSR_TURBO_RATIO_LIMIT",
    "^ 1",
    "wrmsr64(kMSRHWPEnable",
):
    assert forbidden not in driver, forbidden

for required in (
    "restoreLocked(kTurboMacRestoreWatchdog)",
    "restoreLocked(kTurboMacRestoreInvalidRequest)",
    "restoreLocked(kTurboMacRestoreReadbackMismatch)",
    "mp_rendezvous_no_intrs(powerControlOnCPU",
    "setPowerControlGuardLocked(false)",
    "setPowerControlGuardLocked(true)",
    "mp_rendezvous_no_intrs(hwpRequestOnCPU",
    "captureHWPStateLocked()",
    "applyHWPMaximumLocked()",
    "verifyHWPOverrideLocked()",
    "restoreHWPLocked()",
    '"com.parham.turbomac.driver"',
    "TurboMacModuleStart",
    "TurboMacModuleStop",
):
    assert required in driver, required

capability_initialization = driver.split(
    "bool TurboMac::initializeCapabilitiesLocked()", 1
)[1].split("bool TurboMac::reserveClientLocked()", 1)[0]
assert "if (supported_ && !allEnabled)" in capability_initialization
assert "setPowerControlGuardLocked(true)" in capability_initialization

watchdog = driver.split("void TurboMac::watchdogFired", 1)[1].split(
    "bool TurboMac::initializeCapabilitiesLocked()", 1
)[0]
assert "ensurePassiveGuardLocked()" in watchdog
assert "sender->setTimeoutMS(kPassiveGuardPollMS)" in watchdog
assert "verifyArmedStateLocked()" in watchdog

arm = driver.split("IOReturn TurboMac::arm", 1)[1].split(
    "IOReturn TurboMac::update", 1
)[0]
assert arm.index("installLimitsLocked(") < arm.index("applyHWPMaximumLocked()")
assert arm.index("applyHWPMaximumLocked()") < arm.index(
    "setPowerControlGuardLocked(false)"
)

restore = driver.split("bool TurboMac::restoreLocked", 1)[1].split(
    "void TurboMac::scheduleWatchdogLocked", 1
)[0]
assert restore.index("setPowerControlGuardLocked(true)") < restore.index(
    "restoreHWPLocked()"
)
assert restore.index("restoreHWPLocked()") < restore.index(
    "wrmsr64(kMSRPackagePowerLimit"
)

disconnect = driver.split("void TurboMac::clientDisconnected()", 1)[1].split(
    "void TurboMac::watchdogFired", 1
)[0]
assert "restoreLocked(kTurboMacRestoreClientClosed)" in disconnect

assert "driver_->invalidClientCommand()" in client
assert "driver_->clientDisconnected()" in client
assert "super::externalMethod(" in client
assert (
    "IOExternalMethodDispatch TurboMacUserClient::dispatchTable_[kTurboMacSelectorCount]"
    in client
)
assert "selector >= kTurboMacSelectorCount" in client
assert "arguments->structureInputSize == 0U" not in client
for payload in (
    "sizeof(TurboMacCapabilities)",
    "sizeof(TurboMacTelemetry)",
    "sizeof(TurboMacLimitRequest)",
    "sizeof(TurboMacCommandRequest)",
    "sizeof(TurboMacDriverStatus)",
    "sizeof(TurboMacHWPStatus)",
):
    assert payload in client, payload
assert "kTurboMacSelectorCount" in (root / "Shared" / "TurboMacProtocol.h").read_text()
assert "kRAPLFallbackInterval = 1.0" in daemon
assert "energyTracker_.sample(" in daemon
assert "energyTracker_.reset()" in daemon
assert "raplSuccesses_ >= kTelemetryRecoverySamples" in daemon
assert "apple_guard_enabled" in daemon
assert "rapl_locked" in daemon
assert "current_power_control_raw" in daemon
assert "calibration precondition failed" in daemon
assert "passiveHWPReady" in daemon
assert "armedHWPVerified" in daemon
assert "hwp_maximum_restricted" in daemon
assert "turboMacCalibrationTierResponsive(limit, averagePackage)" in daemon
assert "calibration_plateau" in daemon
assert "TurboMacCapabilities statusCapabilities" in daemon
assert "driver_.capabilities(\n            &statusCapabilities" in daemon
assert "std::fflush(stdout)" in cli
assert "return sawError ? 1 : 0" in cli
assert "/usr/bin/shasum" in validation
assert "/usr/bin/sudo -n -u" in validation
assert "--threads 8" in validation
assert "--no-gpu" in validation
assert 'KEXT_POLICY_EXIT=27' in installer
assert 'pending-kext-operation' in installer
assert 'INSTALL_COMPLETE=1' in installer
assert 'grep -F "$bundle_id"' in installer
assert 'launchctl-turbomacd.txt' in installer
assert 'live-files-before-sha256.txt' in installer
assert 'turbomac-rollback' in installer
assert 'KEXT_POLICY_EXIT=27' in finalizer
assert 'do not reboot' in finalizer
assert driver_info["OSBundleLibraries"] == {
    "com.apple.kpi.iokit": "20.0.0",
    "com.apple.kpi.libkern": "20.0.0",
    "com.apple.kpi.mach": "20.0.0",
    "com.apple.kpi.unsupported": "20.0.0",
}

print("driver fail-safe source contract tests passed")
