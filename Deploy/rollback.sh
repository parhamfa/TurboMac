#!/bin/bash
set -euo pipefail
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH

if [[ "$(id -u)" -ne 0 ]]; then
  echo "rollback.sh must run as root" >&2
  exit 1
fi
if [[ $# -ne 1 || ! -d "$1" ]]; then
  echo "usage: rollback.sh /Library/Application Support/TurboMac/rollback/TIMESTAMP" >&2
  exit 2
fi

BACKUP_DIR="$1"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
RECOVERY_DIR="$BACKUP_DIR/removed-governor-$STAMP"
install -d -m 0700 -o root -g wheel "$RECOVERY_DIR"

rebuild_collections() {
  if kmutil install -z --volume-root / --update-all --update-preboot; then
    return 0
  fi
  echo "Normal collection rebuild failed; retrying with the explicit macOS 13 missing-KDK override." >&2
  kmutil install -z --volume-root / --update-all --allow-missing-kdk --update-preboot
}

launchctl bootout system/com.parham.turbomacd 2>/dev/null || true
for target in \
  /Library/Extensions/TurboMac.kext \
  /usr/local/libexec/turbomacd \
  /usr/local/libexec/turbomac-avx2-load \
  /usr/local/bin/turbomacctl \
  /Library/LaunchDaemons/com.parham.turbomacd.plist; do
  if [[ -e "$target" ]]; then
    mv "$target" "$RECOVERY_DIR/$(basename "$target")"
  fi
done

if [[ -d "$BACKUP_DIR/staged-original-TurboMac.kext" ]]; then
  ditto "$BACKUP_DIR/staged-original-TurboMac.kext" /Library/Extensions/TurboMac.kext
elif [[ -d "$BACKUP_DIR/TurboMac.kext" ]]; then
  ditto "$BACKUP_DIR/TurboMac.kext" /Library/Extensions/TurboMac.kext
fi
for target in \
  /usr/local/libexec/turbomacd \
  /usr/local/libexec/turbomac-avx2-load \
  /usr/local/bin/turbomacctl \
  /Library/LaunchDaemons/com.parham.turbomacd.plist; do
  saved="$BACKUP_DIR/original-$(basename "$target")"
  if [[ -e "$saved" ]]; then
    ditto "$saved" "$target"
  fi
done

rebuild_collections
echo "Rollback staged and AuxKC rebuilt. Removed governor is recoverable at:"
echo "$RECOVERY_DIR"
echo "Reboot is intentionally not automatic."
