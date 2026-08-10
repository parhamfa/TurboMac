#!/bin/bash
set -euo pipefail
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH

if [[ "$(id -u)" -ne 0 ]]; then
  echo "install-passive.sh must run as root" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PACKAGE_DIR="${1:-$SCRIPT_DIR/../build/package}"
KEXT_SOURCE="$PACKAGE_DIR/Library/Extensions/TurboMac.kext"
KEXT_TARGET="/Library/Extensions/TurboMac.kext"
STATE_ROOT="/Library/Application Support/TurboMac"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
BACKUP_DIR="$STATE_ROOT/rollback/$STAMP"
INSTALL_COMPLETE=0
KEXT_POLICY_EXIT=27

rebuild_collections() {
  local bundle_id
  bundle_id="$(defaults read "$KEXT_TARGET/Contents/Info" CFBundleIdentifier)"
  kmutil load --bundle-path "$KEXT_TARGET"
  kmutil inspect -A /Library/KernelCollections/AuxiliaryKernelExtensions.kc \
    --show-kext-uuids | grep -F "$bundle_id" >/dev/null
}

restore_failed_install() {
  local status=$?
  if [[ "$status" -eq 0 ]]; then
    status=1
  fi
  trap - EXIT HUP INT TERM
  if [[ "$INSTALL_COMPLETE" -eq 1 ]]; then
    return
  fi

  set +e
  echo "Installation failed; restoring staged files. Do not reboot until the original collections are rebuilt." >&2
  local failed_dir="$BACKUP_DIR/failed-install"
  install -d -m 0700 -o root -g wheel "$failed_dir"
  launchctl bootout system/com.parham.turbomacd 2>/dev/null || true

  for target in \
    /Library/Extensions/TurboMac.kext \
    /usr/local/libexec/turbomacd \
    /usr/local/libexec/turbomac-avx2-load \
    /usr/local/libexec/turbomac-finalize-passive \
    /usr/local/libexec/turbomac-rollback \
    /usr/local/bin/turbomacctl \
    /usr/local/bin/turbomactop \
    /Library/LaunchDaemons/com.parham.turbomacd.plist; do
    if [[ -e "$target" ]]; then
      mv "$target" "$failed_dir/$(basename "$target")"
    fi
  done

  if [[ -d "$BACKUP_DIR/staged-original-TurboMac.kext" ]]; then
    mv "$BACKUP_DIR/staged-original-TurboMac.kext" "$KEXT_TARGET"
  elif [[ -d "$BACKUP_DIR/TurboMac.kext" ]]; then
    ditto "$BACKUP_DIR/TurboMac.kext" "$KEXT_TARGET"
  fi
  for target in \
    /usr/local/libexec/turbomacd \
    /usr/local/libexec/turbomac-avx2-load \
    /usr/local/libexec/turbomac-finalize-passive \
    /usr/local/libexec/turbomac-rollback \
    /usr/local/bin/turbomacctl \
    /usr/local/bin/turbomactop \
    /Library/LaunchDaemons/com.parham.turbomacd.plist; do
    local saved
    saved="$BACKUP_DIR/original-$(basename "$target")"
    if [[ -e "$saved" ]]; then
      ditto "$saved" "$target"
    fi
  done

  if rebuild_collections >"$BACKUP_DIR/failed-install-restore-kmutil.log" 2>&1; then
    echo "Original files and kernel collections restored; the failed payload is recoverable at $failed_dir" >&2
  else
    echo "Original files were restored but the collection rebuild also failed. Do not reboot; inspect $BACKUP_DIR/failed-install-restore-kmutil.log" >&2
  fi
  exit "$status"
}

if [[ ! -d "$KEXT_SOURCE" ]]; then
  echo "package not found: $PACKAGE_DIR" >&2
  exit 1
fi
if [[ -e /System/Library/Extensions/TurboMac.kext ]]; then
  echo "refusing installation: a TurboMac bundle exists in /System/Library" >&2
  exit 1
fi

codesign --verify --strict --verbose=4 "$KEXT_SOURCE"
kmutil print-diagnostics -a x86_64 -z -p "$KEXT_SOURCE"

install -d -m 0755 -o root -g wheel "$STATE_ROOT"
install -d -m 0700 -o root -g wheel "$STATE_ROOT/rollback" "$BACKUP_DIR"
sw_vers >"$BACKUP_DIR/sw_vers.txt"
uname -a >"$BACKUP_DIR/uname.txt"
csrutil status >"$BACKUP_DIR/sip.txt" 2>&1 || true
csrutil authenticated-root status >"$BACKUP_DIR/authenticated-root.txt" 2>&1 || true
kmutil showloaded >"$BACKUP_DIR/kmutil-showloaded.txt" 2>&1 || true
kextstat >"$BACKUP_DIR/kextstat.txt" 2>&1 || true
launchctl print system/com.parham.turbomacd \
  >"$BACKUP_DIR/launchctl-turbomacd.txt" 2>&1 || true
ps -axo pid,ppid,user,lstart,command \
  >"$BACKUP_DIR/processes-before.txt" 2>&1 || true
for live_file in \
  /Library/Extensions/TurboMac.kext/Contents/MacOS/TurboMac \
  /usr/local/libexec/turbomacd \
  /usr/local/libexec/turbomac-avx2-load \
  /usr/local/bin/turbomacctl \
  /usr/local/bin/turbomactop \
  /Library/LaunchDaemons/com.parham.turbomacd.plist; do
  if [[ -f "$live_file" ]]; then
    shasum -a 256 "$live_file" >>"$BACKUP_DIR/live-files-before-sha256.txt"
  fi
done
if [[ -f /Library/KernelCollections/AuxiliaryKernelExtensions.kc ]]; then
  ditto /Library/KernelCollections/AuxiliaryKernelExtensions.kc "$BACKUP_DIR/AuxiliaryKernelExtensions.kc.before"
  shasum -a 256 /Library/KernelCollections/AuxiliaryKernelExtensions.kc >"$BACKUP_DIR/auxkc-before-sha256.txt"
fi
find /System/Volumes/Preboot -type f -name '*.kc' -exec shasum -a 256 {} \; \
  >"$BACKUP_DIR/preboot-kc-before-sha256.txt" 2>/dev/null || true
find "$PACKAGE_DIR" -type f -exec shasum -a 256 {} \; \
  >"$BACKUP_DIR/package-sha256.txt"

trap restore_failed_install EXIT HUP INT TERM

if [[ -d "$KEXT_TARGET" ]]; then
  ditto "$KEXT_TARGET" "$BACKUP_DIR/TurboMac.kext"
  codesign -dvvv "$KEXT_TARGET" >"$BACKUP_DIR/old-codesign.txt" 2>&1 || true
  find "$KEXT_TARGET" -type f -exec shasum -a 256 {} \; >"$BACKUP_DIR/old-sha256.txt"
  mv "$KEXT_TARGET" "$BACKUP_DIR/staged-original-TurboMac.kext"
fi
for target in \
  /usr/local/libexec/turbomacd \
  /usr/local/libexec/turbomac-avx2-load \
  /usr/local/libexec/turbomac-finalize-passive \
  /usr/local/libexec/turbomac-rollback \
  /usr/local/bin/turbomacctl \
  /usr/local/bin/turbomactop \
  /Library/LaunchDaemons/com.parham.turbomacd.plist; do
  if [[ -e "$target" ]]; then
    ditto "$target" "$BACKUP_DIR/original-$(basename "$target")"
  fi
done

ditto "$KEXT_SOURCE" "$KEXT_TARGET"
chown -R root:wheel "$KEXT_TARGET"
find "$KEXT_TARGET" -type d -exec chmod 0755 {} \;
find "$KEXT_TARGET" -type f -exec chmod 0644 {} \;
chmod 0755 "$KEXT_TARGET/Contents/MacOS/TurboMac"

install -d -m 0755 -o root -g wheel /usr/local/bin /usr/local/libexec
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/bin/turbomacctl" /usr/local/bin/turbomacctl
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/bin/turbomactop" /usr/local/bin/turbomactop
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/libexec/turbomacd" /usr/local/libexec/turbomacd
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/libexec/turbomac-avx2-load" /usr/local/libexec/turbomac-avx2-load
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/libexec/turbomac-finalize-passive" /usr/local/libexec/turbomac-finalize-passive
install -m 0755 -o root -g wheel "$PACKAGE_DIR/usr/local/libexec/turbomac-rollback" /usr/local/libexec/turbomac-rollback
install -m 0644 -o root -g wheel "$PACKAGE_DIR/Library/LaunchDaemons/com.parham.turbomacd.plist" /Library/LaunchDaemons/com.parham.turbomacd.plist

codesign --verify --strict --verbose=4 "$KEXT_TARGET"
kmutil print-diagnostics -a x86_64 -z -p "$KEXT_TARGET"

set +e
kmutil load --bundle-path "$KEXT_TARGET"
KMUTIL_STATUS=$?
set -e
if [[ "$KMUTIL_STATUS" -ne 0 && "$KMUTIL_STATUS" -ne "$KEXT_POLICY_EXIT" ]]; then
  exit "$KMUTIL_STATUS"
fi

find "$KEXT_TARGET" -type f -exec shasum -a 256 {} \; >"$BACKUP_DIR/new-sha256.txt"
echo "$BACKUP_DIR" >"$STATE_ROOT/latest-rollback-path"
chmod 0600 "$STATE_ROOT/latest-rollback-path"

if [[ "$KMUTIL_STATUS" -eq "$KEXT_POLICY_EXIT" ]]; then
  {
    echo "install"
    echo "com.parham.turbomac.driver"
    echo "$BACKUP_DIR"
  } >"$STATE_ROOT/pending-kext-operation"
  chmod 0600 "$STATE_ROOT/pending-kext-operation"
  INSTALL_COMPLETE=1
  echo "Passive governor files staged; macOS user approval is required."
  echo "Approve TurboMac in Privacy & Security, do not reboot, then run:"
  echo "  sudo /usr/local/libexec/turbomac-finalize-passive"
  echo "Rollback: $BACKUP_DIR"
  exit 0
fi

kmutil inspect -A /Library/KernelCollections/AuxiliaryKernelExtensions.kc \
  --show-kext-uuids | grep -F "com.parham.turbomac.driver" >/dev/null
shasum -a 256 /Library/KernelCollections/AuxiliaryKernelExtensions.kc \
  >"$BACKUP_DIR/auxkc-after-sha256.txt"
find /System/Volumes/Preboot -type f -name '*.kc' -exec shasum -a 256 {} \; \
  >"$BACKUP_DIR/preboot-kc-after-sha256.txt" 2>/dev/null || true
rm -f "$STATE_ROOT/pending-kext-operation"
INSTALL_COMPLETE=1

echo "Passive governor staged and AuxKC rebuilt."
echo "Rollback: $BACKUP_DIR"
echo "Reboot is intentionally not automatic."
