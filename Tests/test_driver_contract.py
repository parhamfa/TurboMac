#!/usr/bin/env python3
import plistlib
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = (root / "TurboMac" / "TurboMac.cpp").read_text()
client = (root / "TurboMac" / "TurboMacUserClient.cpp").read_text()
daemon = (root / "Daemon" / "main.cpp").read_text()
validation = (root / "Deploy" / "whisper-validation.example").read_text()
installer = (root / "Deploy" / "install-passive.sh").read_text()
finalizer = (root / "Deploy" / "finalize-passive.sh").read_text()
with (root / "TurboMac" / "Info.plist").open("rb") as plist_file:
    driver_info = plistlib.load(plist_file)

for forbidden in (
    "IA32_HWP_REQUEST",
    "IA32_PM_ENABLE",
    "IA32_MISC_ENABLE",
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
    '"com.parham.turbomac.driver"',
    "TurboMacModuleStart",
    "TurboMacModuleStop",
):
    assert required in driver, required

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
):
    assert payload in client, payload
assert "kTurboMacSelectorCount" in (root / "Shared" / "TurboMacProtocol.h").read_text()
assert "kRAPLFallbackInterval = 1.0" in daemon
assert "energyTracker_.sample(" in daemon
assert "energyTracker_.reset()" in daemon
assert "raplSuccesses_ >= kTelemetryRecoverySamples" in daemon
assert "/usr/bin/shasum" in validation
assert "/usr/bin/sudo -n -u" in validation
assert "--threads 8" in validation
assert "--no-gpu" in validation
assert 'KEXT_POLICY_EXIT=27' in installer
assert 'pending-kext-operation' in installer
assert 'INSTALL_COMPLETE=1' in installer
assert 'grep -F "$bundle_id"' in installer
assert 'KEXT_POLICY_EXIT=27' in finalizer
assert 'do not reboot' in finalizer
assert driver_info["OSBundleLibraries"] == {
    "com.apple.kpi.iokit": "20.0.0",
    "com.apple.kpi.libkern": "20.0.0",
    "com.apple.kpi.mach": "20.0.0",
    "com.apple.kpi.unsupported": "20.0.0",
}

print("driver fail-safe source contract tests passed")
