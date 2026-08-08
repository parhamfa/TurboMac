#!/bin/bash
set -euo pipefail
PATH=/usr/bin:/bin:/usr/sbin:/sbin
export PATH

if [[ "$(id -u)" -ne 0 ]]; then
  echo "configure-whisper-validation.sh must run as root" >&2
  exit 1
fi
if [[ $# -ne 6 ]]; then
  echo "usage: $0 WHISPER_CLI MODEL AUDIO RUN_AS_USER GGML_PREFIX LIBOMP_DYLIB" >&2
  exit 2
fi

WHISPER_CLI="$1"
MODEL="$2"
AUDIO="$3"
RUN_AS_USER="$4"
GGML_PREFIX="$5"
LIBOMP_DYLIB="$6"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEMPLATE="$SCRIPT_DIR/whisper-validation.example"
ROOT="/Library/Application Support/TurboMac"
FIXTURE="$ROOT/validation"
WHISPER_PREFIX="$(cd "$(dirname "$WHISPER_CLI")/.." && pwd -P)"

if [[ ! "$RUN_AS_USER" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "invalid validation account" >&2
  exit 2
fi
RUN_UID="$(id -u "$RUN_AS_USER")"
RUN_GID="$(id -g "$RUN_AS_USER")"
for source in \
  "$WHISPER_CLI" \
  "$MODEL" \
  "$AUDIO" \
  "$WHISPER_PREFIX/lib/libwhisper.1.dylib" \
  "$GGML_PREFIX/lib/libggml.0.dylib" \
  "$GGML_PREFIX/lib/libggml-base.0.dylib" \
  "$GGML_PREFIX/libexec/libggml-cpu.so" \
  "$GGML_PREFIX/libexec/libggml-blas.so" \
  "$LIBOMP_DYLIB" \
  "$TEMPLATE"; do
  if [[ ! -f "$source" ]]; then
    echo "required validation input is missing: $source" >&2
    exit 1
  fi
done

install -d -m 0755 -o root -g wheel \
  "$ROOT" "$FIXTURE" "$FIXTURE/bin" "$FIXTURE/lib" "$FIXTURE/libexec"
install -m 0755 -o root -g wheel "$WHISPER_CLI" "$FIXTURE/bin/whisper-cli"
install -m 0644 -o root -g wheel \
  "$WHISPER_PREFIX/lib/libwhisper.1.dylib" "$FIXTURE/lib/libwhisper.1.dylib"
install -m 0644 -o root -g wheel \
  "$GGML_PREFIX/lib/libggml.0.dylib" "$FIXTURE/lib/libggml.0.dylib"
install -m 0644 -o root -g wheel \
  "$GGML_PREFIX/lib/libggml-base.0.dylib" "$FIXTURE/lib/libggml-base.0.dylib"
install -m 0755 -o root -g wheel \
  "$GGML_PREFIX/libexec/libggml-cpu.so" "$FIXTURE/libexec/libggml-cpu.so"
install -m 0755 -o root -g wheel \
  "$GGML_PREFIX/libexec/libggml-blas.so" "$FIXTURE/libexec/libggml-blas.so"
install -m 0644 -o root -g wheel "$LIBOMP_DYLIB" "$FIXTURE/lib/libomp.dylib"
install -m 0444 -o root -g wheel "$MODEL" "$FIXTURE/model.bin"
install -m 0444 -o root -g wheel "$AUDIO" "$FIXTURE/audio.wav"

rewrite_dependency() {
  local binary="$1"
  local dependency_name="$2"
  local replacement="$3"
  local dependency
  local rewritten=0

  while IFS= read -r dependency; do
    if [[ "${dependency##*/}" == "$dependency_name" ]]; then
      /usr/bin/install_name_tool -change "$dependency" "$replacement" "$binary"
      rewritten=$((rewritten + 1))
    fi
  done < <(/usr/bin/otool -L "$binary" | /usr/bin/tail -n +2 | /usr/bin/awk '{print $1}')

  if [[ "$rewritten" -ne 1 ]]; then
    echo "expected exactly one $dependency_name dependency in $binary" >&2
    exit 1
  fi
}

rewrite_dependency "$FIXTURE/bin/whisper-cli" \
  libwhisper.1.dylib @loader_path/../lib/libwhisper.1.dylib
rewrite_dependency "$FIXTURE/bin/whisper-cli" \
  libggml.0.dylib @loader_path/../lib/libggml.0.dylib
rewrite_dependency "$FIXTURE/bin/whisper-cli" \
  libggml-base.0.dylib @loader_path/../lib/libggml-base.0.dylib
/usr/bin/install_name_tool -id @rpath/libwhisper.1.dylib \
  "$FIXTURE/lib/libwhisper.1.dylib"
rewrite_dependency "$FIXTURE/lib/libwhisper.1.dylib" \
  libggml.0.dylib @loader_path/libggml.0.dylib
rewrite_dependency "$FIXTURE/lib/libwhisper.1.dylib" \
  libggml-base.0.dylib @loader_path/libggml-base.0.dylib
/usr/bin/install_name_tool -id @rpath/libggml.0.dylib \
  "$FIXTURE/lib/libggml.0.dylib"
rewrite_dependency "$FIXTURE/lib/libggml.0.dylib" \
  libggml-base.0.dylib @loader_path/libggml-base.0.dylib
/usr/bin/install_name_tool -id @rpath/libggml-base.0.dylib \
  "$FIXTURE/lib/libggml-base.0.dylib"
rewrite_dependency "$FIXTURE/lib/libggml-base.0.dylib" \
  libomp.dylib @loader_path/libomp.dylib
rewrite_dependency "$FIXTURE/libexec/libggml-cpu.so" \
  libggml-base.0.dylib @loader_path/../lib/libggml-base.0.dylib
rewrite_dependency "$FIXTURE/libexec/libggml-cpu.so" \
  libomp.dylib @loader_path/../lib/libomp.dylib
rewrite_dependency "$FIXTURE/libexec/libggml-blas.so" \
  libggml-base.0.dylib @loader_path/../lib/libggml-base.0.dylib
/usr/bin/install_name_tool -id @rpath/libomp.dylib "$FIXTURE/lib/libomp.dylib"

for binary in \
  "$FIXTURE/bin/whisper-cli" \
  "$FIXTURE/lib/libwhisper.1.dylib" \
  "$FIXTURE/lib/libggml.0.dylib" \
  "$FIXTURE/lib/libggml-base.0.dylib" \
  "$FIXTURE/lib/libomp.dylib" \
  "$FIXTURE/libexec/libggml-cpu.so" \
  "$FIXTURE/libexec/libggml-blas.so"; do
  /usr/bin/codesign --force --sign - --timestamp=none "$binary"
done

{
  echo "RUN_UID=$RUN_UID"
  echo "RUN_GID=$RUN_GID"
} >"$FIXTURE/runtime.conf"
chown root:wheel "$FIXTURE/runtime.conf"
chmod 0444 "$FIXTURE/runtime.conf"

(
  cd "$FIXTURE"
  /usr/bin/shasum -a 256 \
    bin/whisper-cli \
    lib/libwhisper.1.dylib \
    lib/libggml.0.dylib \
    lib/libggml-base.0.dylib \
    lib/libomp.dylib \
    libexec/libggml-cpu.so \
    libexec/libggml-blas.so \
    model.bin \
    audio.wav \
    runtime.conf >sha256.txt
)
chown -R root:wheel "$FIXTURE"
find "$FIXTURE" -type d -exec chmod 0755 {} \;
chmod 0555 "$FIXTURE/bin/whisper-cli" \
  "$FIXTURE/libexec/libggml-cpu.so" "$FIXTURE/libexec/libggml-blas.so"
chmod 0444 "$FIXTURE/lib/"* "$FIXTURE/model.bin" "$FIXTURE/audio.wav" \
  "$FIXTURE/runtime.conf" "$FIXTURE/sha256.txt"

if ! /usr/bin/strings "$FIXTURE/lib/libggml.0.dylib" \
    | /usr/bin/grep -Fx "$FIXTURE/libexec" >/dev/null; then
  echo "GGML was not built for the protected validation backend directory" >&2
  exit 1
fi
BACKEND_PROBE="$({
  cd "$FIXTURE"
  /usr/bin/sudo -n -u "#$RUN_UID" -g "#$RUN_GID" -- \
    /usr/bin/env -i HOME=/var/empty PATH=/usr/bin:/bin \
      "$FIXTURE/bin/whisper-cli" --help
} 2>&1)" || {
  echo "fixed Whisper runtime probe failed" >&2
  exit 1
}
if /usr/bin/grep -E '/usr/local/|/opt/homebrew/' <<<"$BACKEND_PROBE" >/dev/null; then
  echo "fixed Whisper runtime escaped to a mutable package-manager backend" >&2
  exit 1
fi
for backend in libggml-cpu.so libggml-blas.so; do
  if ! /usr/bin/grep -F "$FIXTURE/libexec/$backend" <<<"$BACKEND_PROBE" >/dev/null; then
    echo "fixed Whisper runtime did not load $backend from its protected bundle" >&2
    exit 1
  fi
done
install -m 0555 -o root -g wheel "$TEMPLATE" "$ROOT/whisper-validation"

if otool -L "$FIXTURE/bin/whisper-cli" "$FIXTURE/lib/"* "$FIXTURE/libexec/"* \
    | grep '/usr/local/' >/dev/null; then
  echo "validation bundle still contains a mutable Homebrew library reference" >&2
  exit 1
fi
echo "Fixed Whisper validation bundle configured; it has not been executed."
