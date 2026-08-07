#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-run}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

build_and_test() {
  make test
  make verify
}

case "$MODE" in
  run)
    build_and_test
    echo "Built and tested the x86_64 passive-first deployment package at $ROOT_DIR/build/package"
    ;;
  --verify|verify)
    build_and_test
    shasum -a 256 \
      build/Release/TurboMac.kext/Contents/MacOS/TurboMac \
      build/Release/turbomacd \
      build/Release/turbomacctl \
      build/Release/turbomac-avx2-load
    ;;
  --logs|logs)
    if [[ -r /var/log/turbomacd.jsonl ]]; then
      tail -n 200 -f /var/log/turbomacd.jsonl
    else
      echo "No local turbomacd log is readable; this development Mac does not run the x86_64 KEXT." >&2
      exit 1
    fi
    ;;
  --telemetry|telemetry)
    /usr/bin/log stream --info --style compact --predicate 'process == "turbomacd"'
    ;;
  --debug|debug)
    build_and_test
    lldb -- build/Release/turbomacd
    ;;
  *)
    echo "usage: $0 [run|--debug|--logs|--telemetry|--verify]" >&2
    exit 2
    ;;
esac
