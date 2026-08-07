#!/bin/bash
set -euo pipefail
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH

if [[ "$(id -u)" -ne 0 ]]; then
  echo "turbomac-finalize-passive must run as root" >&2
  exit 1
fi

STATE_ROOT="/Library/Application Support/TurboMac"
PENDING="$STATE_ROOT/pending-kext-operation"
KEXT_TARGET="/Library/Extensions/TurboMac.kext"
KEXT_POLICY_EXIT=27

if [[ ! -f "$PENDING" ]]; then
  echo "no pending TurboMac KEXT operation" >&2
  exit 2
fi

MODE="$(sed -n '1p' "$PENDING")"
EXPECTED_ID="$(sed -n '2p' "$PENDING")"
BACKUP_DIR="$(sed -n '3p' "$PENDING")"
if [[ "$MODE" != "install" || "$EXPECTED_ID" != "com.parham.turbomac.driver" || ! -d "$BACKUP_DIR" ]]; then
  echo "invalid pending TurboMac KEXT operation" >&2
  exit 2
fi

INSTALLED_ID="$(defaults read "$KEXT_TARGET/Contents/Info" CFBundleIdentifier)"
if [[ "$INSTALLED_ID" != "$EXPECTED_ID" ]]; then
  echo "installed KEXT identity changed while approval was pending" >&2
  exit 1
fi

codesign --verify --strict --verbose=4 "$KEXT_TARGET"
kmutil print-diagnostics -a x86_64 -z -p "$KEXT_TARGET"

contains_governor() {
  kmutil inspect -A /Library/KernelCollections/AuxiliaryKernelExtensions.kc \
    --show-kext-uuids 2>/dev/null | grep -F "$EXPECTED_ID" >/dev/null
}

if ! contains_governor; then
  set +e
  kmutil load --bundle-path "$KEXT_TARGET"
  KMUTIL_STATUS=$?
  set -e
  if [[ "$KMUTIL_STATUS" -ne 0 && "$KMUTIL_STATUS" -ne "$KEXT_POLICY_EXIT" ]]; then
    exit "$KMUTIL_STATUS"
  fi

  for _ in $(seq 1 30); do
    if contains_governor; then
      break
    fi
    sleep 1
  done
fi

if ! contains_governor; then
  echo "TurboMac approval or AuxKC rebuild is still pending; do not reboot" >&2
  exit "$KEXT_POLICY_EXIT"
fi

shasum -a 256 /Library/KernelCollections/AuxiliaryKernelExtensions.kc \
  >"$BACKUP_DIR/auxkc-after-sha256.txt"
find /System/Volumes/Preboot -type f -name '*.kc' -exec shasum -a 256 {} \; \
  >"$BACKUP_DIR/preboot-kc-after-sha256.txt" 2>/dev/null || true
rm -f "$PENDING"

echo "Passive governor AuxKC finalized."
echo "Rollback: $BACKUP_DIR"
echo "Reboot is intentionally not automatic."
