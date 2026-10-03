#!/usr/bin/env bash
# Build the 'Splosion Man x86_64 AppImage.
#
# Artifacts expected (override via env):
#   SPLOSION_BIN     - game executable (default: out/build/linux-release/splosionman)
#   LAUNCHER_BIN   - Qt launcher (default: out/build/launcher/build/launcher-linux/splosionman_launcher)
#   RUNTIME_LIB    - librexruntime.so (default: thirdparty/rexglue-sdk/out/linux-amd64/librexruntime.so)
#   GPU_LIB        - librexgpu-xenos.so (default: thirdparty/rexglue-sdk/out/linux-amd64/librexgpu-xenos.so)
#   EXTRACT_XISO_BIN - extract-xiso binary (default: out/appimage-tools/extract-xiso/build-x86_64/extract-xiso)
#   LINUXDEPLOY / APPIMAGETOOL - tool paths (default: out/appimage-tools/*-x86_64.AppImage)
#
# No game data (XEX/ISO) is bundled: the launcher imports the user's own
# files at runtime.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/out/build/linux-amd64-release}"
APPDIR="${APPDIR:-$ROOT/out/AppDir}"
OUTPUT="${OUTPUT:-$ROOT/out/SplosionMan-x86_64.AppImage}"

SPLOSION_BIN="${SPLOSION_BIN:-$BUILD_DIR/splosionman}"
LAUNCHER_BIN="${LAUNCHER_BIN:-$ROOT/launcher-linux/build/splosionman_launcher}"
SDK_OUT="${SDK_OUT:-$ROOT/../dantes-build/thirdparty/rexglue-sdk/out/linux-amd64/Release}"
RUNTIME_LIB="${RUNTIME_LIB:-$SDK_OUT/librexruntime.so}"
GPU_LIB="${GPU_LIB:-$SDK_OUT/librexgpu-xenos.so}"
EXTRACT_XISO_BIN="${EXTRACT_XISO_BIN:-$ROOT/../dantes-build/out/appimage-tools/extract-xiso/build-x86_64/extract-xiso}"
EXTRACT_XISO_LICENSE="$ROOT/../dantes-build/out/appimage-tools/extract-xiso/LICENSE.TXT"

TOOLS_DIR="$ROOT/../dantes-build/out/appimage-tools"
# Tools run extracted (no FUSE in build env); fall back to AppImages elsewhere.
if [[ -x "$TOOLS_DIR/linuxdeploy.extracted/AppRun" ]]; then
  LINUXDEPLOY="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy.extracted/AppRun}"
  QT_PLUGIN_BIN="$TOOLS_DIR/linuxdeploy-plugin-qt.extracted/AppRun"
  APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS_DIR/appimagetool.extracted/AppRun}"
else
  LINUXDEPLOY="${LINUXDEPLOY:-$TOOLS_DIR/linuxdeploy-x86_64.AppImage}"
  QT_PLUGIN_BIN="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
  APPIMAGETOOL="${APPIMAGETOOL:-$TOOLS_DIR/appimagetool-x86_64.AppImage}"
fi
QT_PLUGIN="${QT_PLUGIN:-$QT_PLUGIN_BIN}"

if [[ "$(uname -m)" != "x86_64" && "${ALLOW_CROSS_PACKAGE:-0}" != "1" ]]; then
  echo "This package must be assembled on x86_64." >&2
  exit 1
fi

for artifact in "$SPLOSION_BIN" "$LAUNCHER_BIN" "$RUNTIME_LIB" "$GPU_LIB" "$EXTRACT_XISO_BIN"; do
  if [[ ! -f "$artifact" ]]; then
    echo "Missing x86_64 artifact: $artifact" >&2
    exit 1
  fi
done
for tool in "$LINUXDEPLOY" "$QT_PLUGIN" "$APPIMAGETOOL"; do
  if [[ ! -x "$tool" ]]; then
    echo "Tool not executable: $tool" >&2
    exit 1
  fi
done
if ! command -v qmake6 >/dev/null 2>&1; then
  echo "Qt 6 qmake6 not found in PATH." >&2
  exit 1
fi
export QMAKE="$(command -v qmake6)"

# Safety: never bundle game data.
if [[ -f "$ROOT/game/default.xex" ]]; then
  echo "NOTE: $ROOT/game/default.xex exists but will NOT be bundled." >&2
fi

rm -rf "$APPDIR"
install -d \
  "$APPDIR/usr/bin" \
  "$APPDIR/usr/lib" \
  "$APPDIR/usr/libexec" \
  "$APPDIR/usr/share/applications" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps" \
  "$APPDIR/usr/share/licenses/extract-xiso"

install -m755 "$SPLOSION_BIN" "$APPDIR/usr/bin/splosionman"
install -m755 "$LAUNCHER_BIN" "$APPDIR/usr/bin/splosionman_launcher"
install -m755 "$RUNTIME_LIB" "$APPDIR/usr/lib/librexruntime.so"
install -m755 "$GPU_LIB" "$APPDIR/usr/lib/librexgpu-xenos.so"
install -m755 "$GPU_LIB" "$APPDIR/usr/bin/librexgpu-xenos.so"
install -m755 "$EXTRACT_XISO_BIN" "$APPDIR/usr/libexec/extract-xiso"
install -m644 "$ROOT/packaging/appimage/splosion-man.desktop" \
  "$APPDIR/usr/share/applications/splosion-man.desktop"
install -m644 "$ROOT/packaging/appimage/splosion-man.svg" \
  "$APPDIR/usr/share/icons/hicolor/scalable/apps/splosion-man.svg"
if [[ -f "$EXTRACT_XISO_LICENSE" ]]; then
  install -m644 "$EXTRACT_XISO_LICENSE" \
    "$APPDIR/usr/share/licenses/extract-xiso/LICENSE.TXT"
fi

# Stage the Qt6 Wayland platform plugins BEFORE linuxdeploy runs so its
# Qt plugin discovers them and pulls in the wayland-shell-integration and
# wayland-graphics-integration-client helper plugins plus the Qt Wayland
# libraries automatically.
WAYLAND_PLUGIN_DIR="/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms"
if [[ ! -d "$WAYLAND_PLUGIN_DIR" && -d "/usr/lib/qt6/plugins/platforms" ]]; then
  WAYLAND_PLUGIN_DIR="/usr/lib/qt6/plugins/platforms"
fi
if [[ -f "$WAYLAND_PLUGIN_DIR/libqwayland-generic.so" ]]; then
  install -m755 -d "$APPDIR/usr/plugins/platforms"
  install -m644 "$WAYLAND_PLUGIN_DIR/libqwayland-generic.so" \
    "$APPDIR/usr/plugins/platforms/"
  if [[ -f "$WAYLAND_PLUGIN_DIR/libqwayland-egl.so" ]]; then
    install -m644 "$WAYLAND_PLUGIN_DIR/libqwayland-egl.so" \
      "$APPDIR/usr/plugins/platforms/"
  fi
else
  echo "WARNING: Qt6 Wayland platform plugin not found; Wayland sessions will fall back to xcb" >&2
fi

# linuxdeploy-plugin-qt runs as an AppImage; expose it the way linuxdeploy expects.
# NOTE: linuxdeploy misdiscovers "linuxdeploy-plugin-qt-x86_64.AppImage" when it
# sits next to the wrapper, so keep raw AppImages out of TOOLS_DIR.
if [[ ! -e "$TOOLS_DIR/linuxdeploy-plugin-qt" ]]; then
  printf '#!/usr/bin/env bash\nexec "%s" "$@"\n' "$QT_PLUGIN_BIN" > "$TOOLS_DIR/linuxdeploy-plugin-qt"
  chmod +x "$TOOLS_DIR/linuxdeploy-plugin-qt"
fi
if compgen -G "$TOOLS_DIR/*-x86_64.AppImage" > /dev/null; then
  mkdir -p "$TOOLS_DIR/downloads"
  mv "$TOOLS_DIR"/linuxdeploy-x86_64.AppImage \
     "$TOOLS_DIR"/linuxdeploy-plugin-qt-x86_64.AppImage \
     "$TOOLS_DIR"/appimagetool-x86_64.AppImage \
     "$TOOLS_DIR/downloads/" 2>/dev/null || true
fi
export PATH="$TOOLS_DIR:$PATH"
# linuxdeploy resolves the pre-staged runtime libs via LD_LIBRARY_PATH.
export LD_LIBRARY_PATH="$APPDIR/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

"$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/splosionman_launcher" \
  --executable "$APPDIR/usr/bin/splosionman" \
  --desktop-file "$APPDIR/usr/share/applications/splosion-man.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/splosion-man.svg" \
  --plugin qt \
  --custom-apprun "$ROOT/packaging/appimage/AppRun"

install -d "$APPDIR/apprun-hooks"
cat > "$APPDIR/apprun-hooks/splosion-platform.sh" <<'HOOK'
# Let Qt auto-select its platform backend: native Wayland on Wayland
# sessions, xcb on X11. Do NOT force QT_QPA_PLATFORM here — forcing xcb
# under XWayland breaks mouse input on some Plasma Wayland setups.
HOOK

if [[ -f "$APPDIR/AppRun" ]] && ! grep -q "splosion-platform" "$APPDIR/AppRun"; then
  # NOTE: our custom AppRun defines HERE (not this_dir like linuxdeploy's).
  sed -i '/^exec /i source "$HERE/apprun-hooks/splosion-platform.sh"' "$APPDIR/AppRun"
fi

# linuxdeploy-plugin-qt does not deploy Qt's Wayland helper plugins, so stage
# them manually: the shell integrations, the client buffer integrations, the
# Qt Wayland client libraries, and any of their library dependencies that are
# not already bundled.
QT6_PLUGIN_ROOT="/usr/lib/x86_64-linux-gnu/qt6/plugins"
if [[ -d "$QT6_PLUGIN_ROOT/wayland-shell-integration" ]]; then
  install -m755 -d "$APPDIR/usr/plugins/wayland-shell-integration" \
                   "$APPDIR/usr/plugins/wayland-graphics-integration-client"
  cp -a "$QT6_PLUGIN_ROOT/wayland-shell-integration/." \
        "$APPDIR/usr/plugins/wayland-shell-integration/"
  cp -a "$QT6_PLUGIN_ROOT/wayland-graphics-integration-client/." \
        "$APPDIR/usr/plugins/wayland-graphics-integration-client/"
  for lib in libQt6WaylandClient.so.6 libQt6WaylandEglClientHwIntegration.so.6; do
    if [[ ! -f "$APPDIR/usr/lib/$lib" ]]; then
      for dir in /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu /usr/lib; do
        if [[ -f "$dir/$lib" ]]; then
          # Dereference symlinks (-L): a preserved symlink would dangle if its
          # versioned target isn't also bundled.
          cp -aL "$dir/$lib" "$APPDIR/usr/lib/"
          break
        fi
      done
    fi
  done
  # NOTE: Do NOT bundle libwayland-client/cursor/egl. The game (SDL) and Qt
  # both need these, but they must match the host compositor's protocols.
  # Bundling them breaks the game's SDL Wayland init; the system copies are
  # always compatible. Qt's own libQt6WaylandClient is bundled above.
  # Pull in any further dependencies of the staged Wayland plugins that are
  # missing from the bundle (skipping core system libs linuxdeploy excludes).
  for plugin in "$APPDIR"/usr/plugins/wayland-shell-integration/*.so \
               "$APPDIR"/usr/plugins/wayland-graphics-integration-client/*.so \
               "$APPDIR"/usr/plugins/platforms/libqwayland*.so; do
    [[ -f "$plugin" ]] || continue
    while IFS= read -r line; do
      dep_path="$(echo "$line" | awk '{print $3}')"
      dep_name="$(echo "$line" | awk '{print $1}')"
      [[ -z "$dep_path" || "$dep_path" == "not" ]] && continue
      case "$dep_name" in
        libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*|libgcc_s*|libstdc++*) continue;;
        # Never bundle the Wayland client libs: SDL (game) and Qt both need
        # them, but they must match the host compositor. Bundled copies break
        # the game's SDL Wayland init.
        libwayland-client*|libwayland-cursor*|libwayland-egl*|libwayland-server*) continue;;
      esac
      if [[ ! -f "$APPDIR/usr/lib/$dep_name" && -f "$dep_path" ]]; then
        cp -aL "$dep_path" "$APPDIR/usr/lib/$dep_name"
      fi
    done < <(ldd "$plugin" 2>/dev/null | grep "=>")
  done
else
  echo "WARNING: Qt6 Wayland helper plugins not found; Wayland sessions will fall back to xcb" >&2
fi

ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
echo "Created: $OUTPUT"
