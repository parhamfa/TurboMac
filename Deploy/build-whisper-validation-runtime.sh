#!/bin/bash
set -euo pipefail
PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH

GGML_VERSION=0.17.0
GGML_SHA256=49ed958226dd75ea13b3b493150181e3a3ca7dc28c20a3d1f00d23e94cbf7a47
WHISPER_VERSION=1.9.1
WHISPER_SHA256=147267177eef7b22ec3d2476dd514d1b12e160e176230b740e3d1bd600118447
BACKEND_DIR="/Library/Application Support/TurboMac/validation/libexec"
LIBOMP_PREFIX="${LIBOMP_PREFIX:-/usr/local/opt/libomp}"

if [[ $# -ne 1 ]]; then
  echo "usage: $0 EMPTY_OUTPUT_DIRECTORY" >&2
  exit 2
fi
if ! command -v cmake >/dev/null; then
  echo "cmake is required" >&2
  exit 1
fi
if [[ ! -f "$LIBOMP_PREFIX/lib/libomp.dylib" ]]; then
  echo "libomp was not found under $LIBOMP_PREFIX" >&2
  exit 1
fi

OUTPUT="$1"
if [[ -e "$OUTPUT" && ! -d "$OUTPUT" ]]; then
  echo "output path is not a directory: $OUTPUT" >&2
  exit 2
fi
/bin/mkdir -p "$OUTPUT"
if [[ -n "$(/usr/bin/find "$OUTPUT" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  echo "output directory must be empty: $OUTPUT" >&2
  exit 2
fi
OUTPUT="$(cd "$OUTPUT" && pwd -P)"

WORK="$(/usr/bin/mktemp -d /tmp/turbomac-whisper-build.XXXXXX)"
cleanup() {
  case "$WORK" in
    /tmp/turbomac-whisper-build.*) /bin/rm -r -- "$WORK" ;;
  esac
}
trap cleanup EXIT

GGML_ARCHIVE="$WORK/ggml.tar.gz"
WHISPER_ARCHIVE="$WORK/whisper.tar.gz"
/usr/bin/curl --fail --location --silent --show-error \
  "https://github.com/ggml-org/ggml/archive/refs/tags/v$GGML_VERSION.tar.gz" \
  --output "$GGML_ARCHIVE"
/usr/bin/curl --fail --location --silent --show-error \
  "https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v$WHISPER_VERSION.tar.gz" \
  --output "$WHISPER_ARCHIVE"
echo "$GGML_SHA256  $GGML_ARCHIVE" | /usr/bin/shasum -a 256 -c -
echo "$WHISPER_SHA256  $WHISPER_ARCHIVE" | /usr/bin/shasum -a 256 -c -

/usr/bin/tar -xzf "$GGML_ARCHIVE" -C "$WORK"
/usr/bin/tar -xzf "$WHISPER_ARCHIVE" -C "$WORK"
GGML_SOURCE="$WORK/ggml-$GGML_VERSION"
WHISPER_SOURCE="$WORK/whisper.cpp-$WHISPER_VERSION"
GGML_PREFIX="$OUTPUT/ggml"
WHISPER_PREFIX="$OUTPUT/whisper"
JOBS="$(/usr/sbin/sysctl -n hw.logicalcpu)"

cmake -S "$GGML_SOURCE" -B "$WORK/ggml-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$GGML_PREFIX" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$LIBOMP_PREFIX" \
  -DBUILD_SHARED_LIBS=ON \
  "-DGGML_BACKEND_DIR=$BACKEND_DIR" \
  -DGGML_BACKEND_DL=ON \
  -DGGML_BLAS=ON \
  -DGGML_BLAS_VENDOR=Apple \
  -DGGML_BUILD_EXAMPLES=OFF \
  -DGGML_BUILD_TESTS=OFF \
  -DGGML_CCACHE=OFF \
  -DGGML_LTO=ON \
  -DGGML_METAL=OFF \
  -DGGML_NATIVE=OFF
cmake --build "$WORK/ggml-build" --parallel "$JOBS"
GGML_STAGE="$WORK/ggml-stage"
DESTDIR="$GGML_STAGE" cmake --install "$WORK/ggml-build"
/usr/bin/ditto "$GGML_STAGE$GGML_PREFIX" "$GGML_PREFIX"
/bin/mkdir -p "$GGML_PREFIX/libexec"
/usr/bin/install -m 0755 \
  "$GGML_STAGE$BACKEND_DIR/libggml-cpu.so" \
  "$GGML_STAGE$BACKEND_DIR/libggml-blas.so" \
  "$GGML_PREFIX/libexec/"

cmake -S "$WHISPER_SOURCE" -B "$WORK/whisper-build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$WHISPER_PREFIX" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$GGML_PREFIX" \
  -DBUILD_SHARED_LIBS=ON \
  -DWHISPER_BUILD_EXAMPLES=ON \
  -DWHISPER_BUILD_SERVER=OFF \
  -DWHISPER_BUILD_TESTS=OFF \
  -DWHISPER_SDL2=OFF \
  -DWHISPER_USE_SYSTEM_GGML=ON
cmake --build "$WORK/whisper-build" --parallel "$JOBS"
cmake --install "$WORK/whisper-build"

for artifact in \
  "$WHISPER_PREFIX/bin/whisper-cli" \
  "$WHISPER_PREFIX/lib/libwhisper.1.dylib" \
  "$GGML_PREFIX/lib/libggml.0.dylib" \
  "$GGML_PREFIX/lib/libggml-base.0.dylib" \
  "$GGML_PREFIX/libexec/libggml-cpu.so" \
  "$GGML_PREFIX/libexec/libggml-blas.so"; do
  if [[ ! -f "$artifact" ]]; then
    echo "build did not produce $artifact" >&2
    exit 1
  fi
done
if ! /usr/bin/strings "$GGML_PREFIX/lib/libggml.0.dylib" \
    | /usr/bin/grep -Fx "$BACKEND_DIR" >/dev/null; then
  echo "GGML build does not contain the protected backend directory" >&2
  exit 1
fi

echo "Pinned validation runtime built under $OUTPUT"
echo "whisper-cli: $WHISPER_PREFIX/bin/whisper-cli"
echo "ggml prefix: $GGML_PREFIX"
