# TurboMac Dynamic RAPL Governor

This fork replaces TurboMac's one-shot HWP and XOR writes with a passive-first,
package-scoped Intel RAPL governor and an explicit, boot-scoped HWP transition.
It was designed specifically for a
2017 MacBook Pro 14,3 with an Intel Core i7-7820HQ, no battery, and a USB-C
input-power failure mode.

This is not a general-purpose thermal utility. The KEXT refuses to arm on a CPU
family/model other than Intel family 6, model `0x9e`. The daemon additionally
binds a calibration profile to the machine UUID, model, CPU brand, SMC key
types, macOS version, and build.

## What it controls

Package RAPL PL1/PL2 provide the actual power limit. The limit applies to the
whole Intel CPU package, so native processes and VM workloads receive the same
package budget. This Mac boots with HWP disabled and a legacy XCPM ceiling. To
escape that ceiling safely, the KEXT can enable HWP only after it has installed
and read back a package limit no higher than 5 W, while Apple's guard is still
enabled. It then installs a conservative HWP request on every logical CPU,
restores the requested governed RAPL limit, releases the HWP maximum, and only
then clears the guard.

`IA32_PM_ENABLE.HWP_ENABLE` cannot be cleared without a processor reset. Thus,
disarm cannot return this boot to its original non-HWP state; it means “Apple
guard on, conservative HWP fail-safe active, captured Apple RAPL restored.” A
reboot is required for native non-HWP restoration. The KEXT never writes
`IA32_PERF_CTL`, changes per-process controls, writes arbitrary MSRs, sets the
RAPL lock bit, or raises a RAPL limit above Apple's captured value.

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

The KEXT starts passive, restores Apple's bidirectional PROCHOT guard if an old
bypass or late macOS power-management initialization leaves it disabled, and
rechecks every second while passive. It permits one root client. If HWP is still
disabled, the first arm follows this order:

1. capture Apple's package limits and verify the guard on every logical CPU;
2. install and read back a RAPL bootstrap limit at or below 5 W;
3. enable HWP and verify `IA32_PM_ENABLE` on every logical CPU, read its
   capabilities and each CPU's default request, then install and verify a
   conservative request everywhere;
4. install and verify the daemon's requested RAPL limits while the conservative
   HWP request and Apple guard are still active;
5. install a balanced autonomous HWP request using Intel's default EPP of 128,
   release its maximum, and verify every logical CPU;
6. only then clear bidirectional PROCHOT enable bit 0 on every logical CPU.

When HWP was already enabled before this driver enabled it, arm instead requires
every logical CPU to have a readable restricted maximum and captures those
requests exactly. On daemon disconnect, malformed commands, unsafe readback,
explicit disarm, driver stop, or a five-second heartbeat timeout, restoration
always enables Apple's guard first. For HWP enabled by TurboMac it then installs
the conservative per-CPU request before restoring Apple's RAPL limits. If that
request cannot be verified, the low RAPL limit is retained rather than restoring
Apple's higher limit. While passive, the watchdog re-verifies both the guard and
the conservative request every second. Status reports whether TurboMac enabled
HWP, whether the fail-safe has exact readback, and whether native restoration
requires a reboot.

Every per-CPU HWP request access first checks that CPU's live
`IA32_PM_ENABLE.HWP_ENABLE` bit. A missing bit or a newly appearing logical CPU
fails the broadcast safely: Apple's guard remains enabled and the low RAPL limit
is retained instead of touching an unavailable request MSR.

Auto-arm requires ten continuous seconds of valid telemetry, a protected
root-owned profile, exact identity matching, and a prior successful fixed
Whisper validation. On a later boot, auto-arm still passes through the same
verified 5 W HWP bootstrap stage.

## Build and test

Xcode 26's toolchain and SDK are used directly.

```sh
./script/build_and_run.sh
```

This runs unit tests, builds all deployment artifacts as `x86_64`, ad-hoc signs
them, verifies signatures and plists, and creates `build/package`. On an x86_64
target it also runs `kmutil print-diagnostics -z` for this explicitly
SIP-disabled, ad-hoc-signed installation. Tests cover RAPL encoding, HWP field
preservation and package/local source selection, 32-bit energy wraparound,
malformed requests, watchdog timing, controller scaling, transitions,
hysteresis, slew limiting, and the 45/52/58 paths.

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
refusing to proceed. macOS return code 27 is handled separately because it is
the expected user-approval gate, not a linker failure: the new files remain
staged and the installer tells the operator to approve TurboMac and run
`sudo /usr/local/libexec/turbomac-finalize-passive` before rebooting.
`Deploy/rollback.sh` moves
new files into a recoverable directory, restores the captured KEXT, and rebuilds
the AuxKC. The scripts use the documented `kmutil load` staging flow so
`kernelmanagerd` rebuilds only the auxiliary collection; they do not invoke the
system-only `kmutil install --update-all` path or attempt to write the sealed
boot/system collections.

The packaged rollback command is:

```sh
sudo /usr/local/libexec/turbomac-rollback \
  "/Library/Application Support/TurboMac/rollback/TIMESTAMP"
```

After the passive reboot and SSH verification, `status` should show HWP
supported but disabled, Apple guard enabled, and the governor disarmed. Then:

```sh
sudo turbomacctl status
sudo turbomacctl calibrate
sudo turbomacctl validate
sudo turbomacctl arm
sudo turbomacctl disarm
sudo turbomacctl logs
```

Calibration requires an interactive confirmation and physical supervision. Its
first 5 W arm may perform the reset-only HWP transition described above. It uses
an eight-thread deterministic AVX2 load, three 30-second runs per 2 W PL1 step,
and repeated five-second PL2 bursts. Progress is streamed as each run is
measured. It aborts on `PDTR >= 52 W`, CPU temperature `>= 95 C`, ACPW change
over 5%, a read failure, or a limit mismatch; it discards a non-responsive RAPL
tier after responsiveness has been established and conservatively stops a sweep
at 50 W. Initial requests below the CPU package's observed loaded floor are
recorded and skipped until the first responsive tier. PL2 testing selects its
cruise base from the completed input/package map plus reserve; it never uses the
temporarily inflated post-load non-CPU filter. Its absolute PL2 ceiling is also
capped at the largest value actually tested from that base. Calibration finishes disarmed,
which leaves HWP enabled under its conservative fail-safe until reboot. It also
leaves auto-arm disabled. Only a successful protected, fixed Whisper workload
enables auto-arm.

`Deploy/build-whisper-validation-runtime.sh` reproducibly builds the pinned GGML
0.17.0 and whisper.cpp 1.9.1 sources with GGML's default backend directory set
to the protected validation bundle. `Deploy/configure-whisper-validation.sh`
then copies the model, audio fixture, binary, and all non-system libraries into
that root-owned, hash-pinned bundle. Configuration refuses a runtime that loads
a mutable Homebrew backend. The runtime helper drops to the nominated
unprivileged account before running two eight-thread CPU-only transcriptions.

Keep an independent observer running during rollout. Do not treat a completed
calibration or a surviving excursion as proof of a universal shutdown boundary.

## License and provenance

Forked from [mndhvn/TurboMac](https://github.com/mndhvn/TurboMac). Original work
copyright Marko Calasan, 2022. Modifications are distributed under the GNU GPL
v3.0; see `LICENSE`. There is no warranty.
