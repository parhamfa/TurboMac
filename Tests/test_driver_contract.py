#!/usr/bin/env python3
import plistlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = (root / "TurboMac" / "TurboMac.cpp").read_text()
client = (root / "TurboMac" / "TurboMacUserClient.cpp").read_text()
daemon = (root / "Daemon" / "main.cpp").read_text()
cli = (root / "CLI" / "main.cpp").read_text()
validation = (root / "Deploy" / "whisper-validation.example").read_text()
validation_configure = (
    root / "Deploy" / "configure-whisper-validation.sh"
).read_text()
validation_builder = (
    root / "Deploy" / "build-whisper-validation-runtime.sh"
).read_text()
installer = (root / "Deploy" / "install-passive.sh").read_text()
finalizer = (root / "Deploy" / "finalize-passive.sh").read_text()
with (root / "TurboMac" / "Info.plist").open("rb") as plist_file:
    driver_info = plistlib.load(plist_file)

for forbidden in (
    "IA32_MISC_ENABLE",
    "IA32_PERF_CTL",
    "MSR_TURBO_RATIO_LIMIT",
    "^ 1",
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
    "applyHWPMaximumLocked(request->hwp_mode)",
    "verifyHWPOverrideLocked()",
    "restoreHWPLocked()",
    "ensureHWPFailSafeLocked()",
    "wrmsr64(kMSRHWPEnable",
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
assert "kHWPBootstrapLimitMW" in arm
assert arm.index("installLimitsLocked(") < arm.index("bootstrapHWPLocked()")
assert arm.index("bootstrapHWPLocked()") < arm.index("applyHWPMaximumLocked(")
assert arm.count("installLimitsLocked(") >= 2
assert arm.rindex("installLimitsLocked(") < arm.index("applyHWPMaximumLocked(")
assert arm.index("applyHWPMaximumLocked(") < arm.index(
    "setPowerControlGuardLocked(false)"
)

bootstrap = driver.split("bool TurboMac::bootstrapHWPLocked", 1)[1].split(
    "bool TurboMac::ensureHWPFailSafeLocked", 1
)[0]
assert bootstrap.index("verifyLimitsLocked(expectedPackageLimit_)") < bootstrap.index(
    "enableHWPPackageLocked()"
)
assert bootstrap.index("allGuardEnabled") < bootstrap.index(
    "enableHWPPackageLocked()"
)
assert bootstrap.index("hwpEnabledByTurboMac_ = true") < bootstrap.index(
    "enableHWPPackageLocked()"
)
assert bootstrap.index("enableHWPPackageLocked()") < bootstrap.index(
    "ensureHWPFailSafeLocked()"
)

hwp_package_enable = driver.split(
    "bool TurboMac::enableHWPPackageLocked", 1
)[1].split("bool TurboMac::hwpEnabledOnCurrentCPULocked", 1)[0]
assert "wrmsr64(kMSRHWPEnable, kHWPEnable)" in hwp_package_enable
assert hwp_package_enable.index("wrmsr64(kMSRHWPEnable") < hwp_package_enable.index(
    "readHWPEnableStateLocked("
)

hwp_request_callback = driver.split(
    "void TurboMac::hwpRequestOnCPU", 1
)[1].split("bool TurboMac::readHWPRequestsLocked", 1)[0]
assert "const uint64_t enable = rdmsr64(kMSRHWPEnable)" in hwp_request_callback
assert "(enable & kHWPEnable) == 0U" in hwp_request_callback
assert hwp_request_callback.index("rdmsr64(kMSRHWPEnable)") < hwp_request_callback.index(
    "rdmsr64(kMSRHWPRequest)"
)
assert hwp_request_callback.index("(enable & kHWPEnable) == 0U") < (
    hwp_request_callback.index("rdmsr64(kMSRHWPRequest)")
)

hwp_package_read = driver.split(
    "bool TurboMac::readHWPPackageRequestLocked", 1
)[1].split("bool TurboMac::writeHWPPackageRequestLocked", 1)[0]
assert hwp_package_read.index("hwpEnabledOnCurrentCPULocked") < (
    hwp_package_read.index("rdmsr64(kMSRHWPPackageRequest)")
)
assert driver.count("rdmsr64(kMSRHWPPackageRequest)") == 1
assert driver.count("wrmsr64(kMSRHWPPackageRequest") == 1

hwp_initialize = driver.split("bool TurboMac::initializeHWPLocked", 1)[1].split(
    "void TurboMac::hwpRequestOnCPU", 1
)[0]
assert hwp_initialize.index("readHWPEnableStateLocked(") < hwp_initialize.index(
    "rdmsr64(kMSRHWPCapabilities)"
)

restricted = driver.split("bool TurboMac::hwpMaximumRestrictedLocked", 1)[1].split(
    "bool TurboMac::applyHWPMaximumLocked", 1
)[0]
assert "bool anyPresent = false" in restricted
assert "effective < lowest || effective >= highest" in restricted
assert "return anyPresent" in restricted

maximum_release = driver.split("bool TurboMac::applyHWPMaximumLocked", 1)[1].split(
    "bool TurboMac::verifyHWPOverrideLocked", 1
)[0]
assert "if (hwpEnabledByTurboMac_)" in maximum_release
assert "tm_hwp_active_request(" in maximum_release

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
assert "hwp_native_restore_requires_reboot" in daemon
assert "Native restoration then requires a reboot" in cli
assert "turboMacCalibrationTierResponsive(limit, averagePackage)" in daemon
assert "turboMacCalibrationBelowEffectiveFloor(" in daemon
assert "calibration_floor_probe" in daemon
assert "calibration_plateau" in daemon
assert "turboMacCalibrationBurstBase(" in daemon
assert "turboMacCalibrationPL2Ceiling(" in daemon
assert "testing PL2 from mapped cruise base" in daemon
assert "TurboMacCapabilities statusCapabilities" in daemon
assert "driver_.capabilities(\n            &statusCapabilities" in daemon
assert "std::fflush(stdout)" in cli
assert "return sawError ? 1 : 0" in cli
assert "/usr/bin/shasum" in validation
assert "/usr/bin/sudo -n -u" in validation
assert "--threads 8" in validation
assert "--no-gpu" in validation
assert "GGML_BACKEND_PATH" not in validation
assert "fixed Whisper runtime escaped" in validation_configure
assert 'grep -Fx "$FIXTURE/libexec"' in validation_configure
assert "GGML_BACKEND_DIR" in validation_builder
assert 'DESTDIR="$GGML_STAGE"' in validation_builder
assert "49ed958226dd75ea13b3b493150181e3a3ca7dc28c20a3d1f00d23e94cbf7a47" in validation_builder
assert "147267177eef7b22ec3d2476dd514d1b12e160e176230b740e3d1bd600118447" in validation_builder
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
assert driver_info["CFBundleShortVersionString"] == "2.2.1"
assert driver_info["CFBundleVersion"] == "2.2.1"
assert '"2.2.1"' in driver

print("driver fail-safe source contract tests passed")
