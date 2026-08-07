# TurboMac Dynamic RAPL Governor

This fork replaces TurboMac's one-shot HWP and XOR writes with a passive-first,
package-scoped Intel RAPL governor. It was designed specifically for a
2017 MacBook Pro 14,3 with an Intel Core i7-7820HQ, no battery, and a USB-C
input-power failure mode.

This is not a general-purpose thermal utility. The KEXT refuses to arm on a CPU
family/model other than Intel family 6, model `0x9e`. The daemon additionally
binds a calibration profile to the machine UUID, model, CPU brand, SMC key
types, macOS version, and build.

## What it controls

Only package RAPL PL1/PL2 are changed. The limit applies to the whole Intel CPU
package, so native processes and VM workloads receive the same package budget.
The governor never writes HWP, per-process controls, arbitrary MSRs, or the RAPL
lock bit, and it never raises either limit above Apple's captured value.

Discrete GPU, display, USB devices, storage, conversion losses, and other power
rails remain outside CPU RAPL. This can reduce CPU-caused input surges; it cannot
guarantee that the Mac will not lose power.

## Control bands

The observed `45/52/58 W` values are controller bands, not proven shutdown-safe
limits. They scale continuously from the median live AppleSMC `ACPW` value:

| Band | Ratio of ACPW | At ACPW 79.496 W |
|---|---:|---:|
| Guard | 56.61% | 45.003 W |
| Shed | 65.41% | 51.998 W |
| Emergency | 72.96% | 58.000 W |

The daemon samples package energy at 10 Hz and AppleSMC `PDTR`/`ACPW` at 4 Hz.
It estimates non-CPU input as `PDTR - package power` with fast-rise/slow-fall
filtering. PL1 is the active state's input target minus that estimate and the
calibrated reserve. Decreases apply immediately; PL1 increases by at most 1 W
per five seconds. PL2 is equal to PL1 outside cruise, where a separately tested
burst allowance may apply. Emergency prediction also takes the more conservative
of the live rail estimate and the calibrated package-to-input mapping.

Transitions use the hysteresis defined in `Daemon/Policy.cpp`: guard requires
two seconds, shed one second, and emergency is immediate from measured or
predicted input. Recovery takes 5/10/15 seconds. If non-CPU load remains above
the emergency band while RAPL is already at its calibrated floor, the daemon
logs `non_cpu_over_budget` and keeps the CPU at that floor.

## Fail-safe boundary

The KEXT starts passive and permits one root client. On arm it:

1. captures Apple's package limits and verifies the guard on every logical CPU;
2. installs and verifies conservative package limits;
3. only then clears bidirectional PROCHOT enable bit 0 on every logical CPU.

It restores Apple's guard before the captured RAPL limits on daemon disconnect,
malformed commands, unsafe readback, explicit disarm, driver stop, or a five-
second heartbeat timeout. Auto-arm requires ten continuous seconds of valid
telemetry, a protected root-owned profile, exact identity matching, and a prior
successful fixed Whisper validation.

## Build and test

Xcode 26's toolchain and SDK are used directly because this repository's legacy
Xcode project can be unusable when the installed Xcode is newer than the local
macOS private frameworks.

```sh
./script/build_and_run.sh
```

This runs unit tests, builds all deployment artifacts as `x86_64`, ad-hoc signs
them, verifies signatures and plists, and creates `build/package`. On an x86_64
target it also runs `kmutil print-diagnostics -z` for this explicitly
SIP-disabled, ad-hoc-signed installation. Tests cover RAPL encoding,
32-bit energy wraparound, malformed requests, watchdog timing, controller
scaling, transitions, hysteresis, slew limiting, and the 45/52/58 paths.

Historical observer logs can be replayed without actuation:

```sh
make replay FILES="/path/to/samples-*.jsonl"
```

Historical `smc_pcpc_w` is only a package-power proxy; calibration uses real
RAPL energy telemetry.

## Supervised rollout

`Deploy/install-passive.sh` backs up the existing KEXT and live kernel state,
installs only under `/Library`, `/usr/local`, and the TurboMac application-
support directory, validates the bundle, and rebuilds the AuxKC. It never edits
`/System/Library` and never reboots automatically. If a rebuild fails, it puts
the original files back and attempts to rebuild their collections before
refusing to proceed. `Deploy/rollback.sh` moves
new files into a recoverable directory, restores the captured KEXT, and rebuilds
the AuxKC.

After the passive reboot and SSH verification:

```sh
sudo turbomacctl status
sudo turbomacctl calibrate
sudo turbomacctl validate
sudo turbomacctl arm
sudo turbomacctl disarm
sudo turbomacctl logs
```

Calibration requires an interactive confirmation and physical supervision. It
uses an eight-thread deterministic AVX2 load, three 30-second runs per 2 W PL1
step, and repeated five-second PL2 bursts. It aborts on `PDTR >= 52 W`, CPU
temperature `>= 95 C`, ACPW change over 5%, a read failure, or a limit mismatch;
it conservatively stops a sweep at 50 W. Calibration leaves auto-arm disabled.
Only a successful protected, fixed Whisper workload enables it.

`Deploy/configure-whisper-validation.sh` copies a model, audio fixture, Whisper
binary, and all non-system libraries into a root-owned, hash-pinned bundle. The
runtime helper still drops to the nominated unprivileged account before running
two eight-thread CPU-only transcriptions.

Keep an independent observer running during rollout. Do not treat a completed
calibration or a surviving excursion as proof of a universal shutdown boundary.

## License and provenance

Forked from [mndhvn/TurboMac](https://github.com/mndhvn/TurboMac). Original work
copyright Marko Calasan, 2022. Modifications are distributed under the GNU GPL
v3.0; see `LICENSE`. There is no warranty.
