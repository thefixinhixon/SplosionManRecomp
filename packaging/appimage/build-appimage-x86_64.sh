#!/usr/bin/env bash
# Package the 'Splosion Man x86_64 AppImage from a staged matched build set.
#
# Env:
#   STAGE_DIR  folder with splosionman, librexruntime.so, librexgpu-xenos.so
#              and TPSplosionmanLauncher (one build set: never mix files)
#   QMAKE      qmake of the Qt the launcher was built with
#   OUTPUT     AppImage path (default: <repo>/out/SplosionMan-x86_64-<VERSION>.AppImage)
#   TOOLS_DIR  where linuxdeploy / appimagetool are downloaded (default: <repo>/out/appimage-tools)
#   APPDIR     scratch AppDir (default: <repo>/out/AppDir)
#
#   GLIBC_MAX    newest glibc symbol version anything shipped may need (default
#                2.34, AlmaLinux 9 / the build container)
#   GLIBCXX_MAX  same for libstdc++ (default 3.4.29, GCC 11)
#                The build fails above either: it means a file was built or
#                bundled from a newer system than the AppImage promises.
#
# Layout (same as v2.x): everything in usr/bin, the launcher is AppRun, Qt
# and the few non-system libraries in usr/lib. libstdc++, libc, X11, xcb and
# Wayland client libraries come from the host (linuxdeploy's exclude list).
# No game data is ever bundled.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
HERE="$ROOT/packaging/appimage"
: "${STAGE_DIR:?set STAGE_DIR to the staged build set}"
: "${QMAKE:?set QMAKE to the Qt qmake}"
VERSION="${VERSION:-dev}"
OUTPUT="${OUTPUT:-$ROOT/out/SplosionMan-x86_64-$VERSION.AppImage}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT/out/appimage-tools}"
APPDIR="${APPDIR:-$ROOT/out/AppDir}"
GLIBC_MAX="${GLIBC_MAX:-2.34}"
GLIBCXX_MAX="${GLIBCXX_MAX:-3.4.29}"

for f in splosionman librexruntime.so librexgpu-xenos.so TPSplosionmanLauncher; do
  [[ -f "$STAGE_DIR/$f" ]] || { echo "Missing $STAGE_DIR/$f" >&2; exit 1; }
done

# Tools, run extracted (containers have no FUSE).
mkdir -p "$TOOLS_DIR"
fetch_tool() {
  local name="$1" url="$2"
  if [[ ! -x "$TOOLS_DIR/$name/AppRun" ]]; then
    curl -fsSL -o "$TOOLS_DIR/$name.AppImage" "$url"
    chmod +x "$TOOLS_DIR/$name.AppImage"
    (cd "$TOOLS_DIR" && "./$name.AppImage" --appimage-extract >/dev/null && mv squashfs-root "$name")
    rm -f "$TOOLS_DIR/$name.AppImage"
  fi
}
fetch_tool linuxdeploy \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch_tool linuxdeploy-plugin-qt \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
fetch_tool appimagetool \
  https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
# linuxdeploy finds plugins on PATH by name.
mkdir -p "$TOOLS_DIR/bin"
printf '#!/bin/sh\nexec "%s" "$@"\n' "$TOOLS_DIR/linuxdeploy-plugin-qt/AppRun" > "$TOOLS_DIR/bin/linuxdeploy-plugin-qt"
chmod +x "$TOOLS_DIR/bin/linuxdeploy-plugin-qt"
export PATH="$TOOLS_DIR/bin:$PATH"

rm -rf "$APPDIR"
install -d "$APPDIR/usr/bin" "$APPDIR/usr/lib"
for f in splosionman librexruntime.so librexgpu-xenos.so TPSplosionmanLauncher; do
  install -m755 "$STAGE_DIR/$f" "$APPDIR/usr/bin/$f"
done
# The game finds librexruntime.so next to itself; so must the GPU plugin
# (its build RUNPATH points into the build tree).
patchelf --set-rpath '$ORIGIN' "$APPDIR/usr/bin/splosionman" "$APPDIR/usr/bin/librexgpu-xenos.so"

export QMAKE
export LD_LIBRARY_PATH="$APPDIR/usr/bin:$("$QMAKE" -query QT_INSTALL_LIBS)${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# Native Wayland for the launcher where the Qt install has it; xcb otherwise.
PLATFORMS_DIR="$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms"
extra=()
for p in libqwayland-generic.so libqwayland-egl.so; do
  [[ -f "$PLATFORMS_DIR/$p" ]] && extra+=("$p")
done
if ((${#extra[@]})); then
  export EXTRA_PLATFORM_PLUGINS="$(IFS=';'; echo "${extra[*]}")"
fi
export NO_STRIP=1  # linuxdeploy's bundled strip is older than our toolchain

"$TOOLS_DIR/linuxdeploy/AppRun" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/TPSplosionmanLauncher" \
  --desktop-file "$HERE/tpsplosion.desktop" \
  --icon-file "$HERE/tpsplosion.png" \
  --plugin qt

# linuxdeploy also deploys the files already in usr/bin; keep one copy of the
# runtime (in usr/bin, where the launcher checks the program set) and point
# the game files back at it.
rm -f "$APPDIR"/usr/lib/librexruntime.so "$APPDIR"/usr/lib/librexgpu-xenos.so
patchelf --set-rpath '$ORIGIN:$ORIGIN/../lib' \
  "$APPDIR/usr/bin/splosionman" "$APPDIR/usr/bin/librexruntime.so" "$APPDIR/usr/bin/librexgpu-xenos.so"
# Never ship Wayland client libraries: SDL (game) and Qt must use the host's,
# which match its compositor. Bundled copies broke the game's Wayland init.
rm -f "$APPDIR"/usr/lib/libwayland-*.so*

# AppRun = the launcher, as in v2.x.
rm -f "$APPDIR/AppRun"
ln -s usr/bin/TPSplosionmanLauncher "$APPDIR/AppRun"

# The newest glibc / libstdc++ symbol versions what we ship requires (the
# ELF version-needs section; same output from every binutils version).
max_version() { # prefix, files...
  local prefix="$1" f; shift
  for f in "$@"; do
    readelf -V --wide "$f" 2>/dev/null | sed -n '/Version needs section/,/^$/p'
  done | grep -o "Name: ${prefix}_[0-9][0-9.]*" | sed "s/^Name: ${prefix}_//" | sort -uV | tail -1
}
newer_than() { [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" != "$2" ]]; }
mapfile -t shipped < <(find "$APPDIR/usr" -type f \( -name '*.so*' -o -perm -u+x \) \
  -exec sh -c 'file -b "$1" | grep -q "^ELF"' _ {} \; -print)
fail=0
for check in "GLIBC $GLIBC_MAX" "GLIBCXX $GLIBCXX_MAX"; do
  set -- $check
  floor="$(max_version "$1" "${shipped[@]}")"
  echo "$1 floor: ${floor:-none} (limit $2)"
  if [[ -n "$floor" ]] && newer_than "$floor" "$2"; then
    fail=1
    for f in "${shipped[@]}"; do
      v="$(max_version "$1" "$f")"
      [[ -n "$v" ]] && newer_than "$v" "$2" && echo "  needs $1_$v: ${f#"$APPDIR"/}" >&2
    done
  fi
done
((fail == 0)) || { echo "shipped files need a newer system than the AppImage promises" >&2; exit 1; }

mkdir -p "$(dirname "$OUTPUT")"
ARCH=x86_64 "$TOOLS_DIR/appimagetool/AppRun" --no-appstream "$APPDIR" "$OUTPUT"
echo "Created: $OUTPUT"
