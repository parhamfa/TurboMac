#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
driver = (root / "TurboMac" / "TurboMac.cpp").read_text()
client = (root / "TurboMac" / "TurboMacUserClient.cpp").read_text()
validation = (root / "Deploy" / "whisper-validation.example").read_text()

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
):
    assert required in driver, required

assert "driver_->invalidClientCommand()" in client
assert "driver_->clientDisconnected()" in client
assert "kTurboMacSelectorCount" in (root / "Shared" / "TurboMacProtocol.h").read_text()
assert "/usr/bin/shasum" in validation
assert "/usr/bin/sudo -n -u" in validation
assert "--threads 8" in validation
assert "--no-gpu" in validation

print("driver fail-safe source contract tests passed")
