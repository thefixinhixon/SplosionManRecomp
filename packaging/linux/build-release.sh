#!/usr/bin/env bash
# Build the release set (game + runtime + GPU plugin + launcher) and package
# the AppImage. Expects the dependencies from deps-almalinux9.sh (the release
# build) or an equivalent system. Run from anywhere; uses this repo checkout.
#
# Env:
#   REXSDK_COMMIT  SDK commit patches/house-sdk.patch was generated from
#                  (default: read from .github/workflows/windows.yml)
#   WORK           scratch dir for the SDK checkout and builds (default: <repo>/out/ci)
#   VERSION        version label for the AppImage file name (default: git describe)
#   QT_DIR         Qt prefix (default: /opt/qt/6.8.3/gcc_64)
#   JOBS           parallel build jobs (default: nproc)
#   LLVM_VERSION   use clang-<N> when installed (default 20); else plain clang
#   GCC_TOOLSET_ROOT  gcc-toolset whose libstdc++ the build uses when present
#                  (default /opt/rh/gcc-toolset-13/root, AlmaLinux 9)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WORK="${WORK:-$ROOT/out/ci}"
QT_DIR="${QT_DIR:-/opt/qt/6.8.3/gcc_64}"
JOBS="${JOBS:-$(nproc)}"
LLVM_VERSION="${LLVM_VERSION:-20}"
GCC_TOOLSET_ROOT="${GCC_TOOLSET_ROOT:-/opt/rh/gcc-toolset-13/root}"
if [[ -z "${REXSDK_COMMIT:-}" ]]; then
  REXSDK_COMMIT="$(sed -n 's/^ *REXSDK_COMMIT: *\([0-9a-f]\{40\}\).*/\1/p' "$ROOT/.github/workflows/windows.yml" | head -1)"
fi
VERSION="${VERSION:-$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)}"
mkdir -p "$WORK"
SDK="$WORK/rexglue-sdk"

# Compilers. With gcc-toolset (the AlmaLinux 9 release container), clang uses
# its libstdc++, which links whatever is newer than the system's GCC 11
# runtime statically into each binary: the result needs only the libstdc++
# every target system already has. See deps-almalinux9.sh.
if command -v "clang++-$LLVM_VERSION" >/dev/null; then
  GAME_CC="clang-$LLVM_VERSION"; GAME_CXX="clang++-$LLVM_VERSION"
else
  GAME_CC=clang; GAME_CXX=clang++
fi
GAME_FLAGS=()
LAUNCHER_CC=gcc-13; LAUNCHER_CXX=g++-13
if [[ -x "$GCC_TOOLSET_ROOT/usr/bin/g++" ]]; then
  gcc_dir="$(dirname "$("$GCC_TOOLSET_ROOT/usr/bin/gcc" -print-libgcc-file-name)")"
  GAME_FLAGS=("-DCMAKE_C_FLAGS=--gcc-install-dir=$gcc_dir" "-DCMAKE_CXX_FLAGS=--gcc-install-dir=$gcc_dir")
  LAUNCHER_CC="$GCC_TOOLSET_ROOT/usr/bin/gcc"; LAUNCHER_CXX="$GCC_TOOLSET_ROOT/usr/bin/g++"
fi
echo "== Compilers: $("$GAME_CXX" --version | head -1) ${GAME_FLAGS[*]}; launcher $("$LAUNCHER_CXX" --version | head -1)"

echo "== SDK $REXSDK_COMMIT + house patch"
if [[ ! -d "$SDK/.git" ]]; then
  git clone --no-checkout https://github.com/rexglue/rexglue-sdk.git "$SDK"
fi
git -C "$SDK" fetch --quiet origin "$REXSDK_COMMIT" || true
git -C "$SDK" checkout --quiet --force "$REXSDK_COMMIT"
git -C "$SDK" clean -fdq
git -C "$SDK" submodule update --init --recursive --quiet
git -C "$SDK" apply --whitespace=nowarn "$ROOT/patches/house-sdk.patch"

echo "== Game + runtime"
GAME_BUILD="$WORK/build-game"
# The repo preset's binaryDir is inside the repo; configure explicitly instead
# so a local run never touches out/build.
cmake -S "$ROOT" -B "$GAME_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$GAME_CC" -DCMAKE_CXX_COMPILER="$GAME_CXX" "${GAME_FLAGS[@]}" \
  -DREXSDK_DIR="$SDK" -DREXGLUE_ENABLE_FIDELITYFX=ON
cmake --build "$GAME_BUILD" -j "$JOBS"

echo "== Launcher"
# OpenGL_GL_PREFERENCE=LEGACY: Qt's CMake config pulls in OpenGL; with GLVND's
# libOpenGL.so present (AlmaLinux) the launcher would otherwise need
# libOpenGL.so.0, which many systems don't install. libGL.so.1 is everywhere.
LAUNCHER_BUILD="$WORK/build-launcher"
cmake -S "$ROOT/tp-launcher" -B "$LAUNCHER_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DTP_GAME=splosionman \
  -DCMAKE_C_COMPILER="$LAUNCHER_CC" -DCMAKE_CXX_COMPILER="$LAUNCHER_CXX" \
  -DOpenGL_GL_PREFERENCE=LEGACY \
  -DCMAKE_PREFIX_PATH="$QT_DIR"
cmake --build "$LAUNCHER_BUILD" -j "$JOBS"

echo "== Stage the matched set"
STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
find_one() {
  local name="$1"; shift
  local f
  f="$(find "$@" -name "$name" -type f -print -quit 2>/dev/null)"
  [[ -n "$f" ]] || { echo "missing build output: $name" >&2; exit 1; }
  echo "$f"
}
cp "$(find_one splosionman "$GAME_BUILD" -maxdepth 1)" "$STAGE/"
cp "$(find_one librexruntime.so "$GAME_BUILD" "$SDK/out")" "$STAGE/"
cp "$(find_one librexgpu-xenos.so "$GAME_BUILD" "$SDK/out")" "$STAGE/"
cp "$(find_one TPSplosionmanLauncher "$LAUNCHER_BUILD" -maxdepth 1)" "$STAGE/"
ls -la "$STAGE"

echo "== AppImage"
STAGE_DIR="$STAGE" QMAKE="$QT_DIR/bin/qmake" VERSION="$VERSION" \
  TOOLS_DIR="$WORK/appimage-tools" APPDIR="$WORK/AppDir" \
  OUTPUT="$WORK/SplosionMan-x86_64-$VERSION.AppImage" \
  "$ROOT/packaging/appimage/build-appimage-x86_64.sh"
