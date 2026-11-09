#!/usr/bin/env bash
# Install the build dependencies for the portable Linux build on Ubuntu 24.04
# (glibc 2.39). Run as root inside an ubuntu:24.04 container; used by
# .github/workflows/linux.yml and by packaging/linux/container-build.sh.
#
# Why 24.04: binaries only run on systems with a glibc at least as new as the
# one they were built against. 2.39 covers the Steam Deck (SteamOS 3.7 ships
# 2.41), Ubuntu 24.04+, Debian 13+, Fedora 40+ and Arch. 22.04 would reach
# further, but its newest libstdc++ (GCC 12) lacks C++20 chrono pieces the SDK
# uses (clock_time_conversion), and bundling a newer libstdc++ is fragile.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

QT_VERSION="${QT_VERSION:-6.8.3}"
QT_ROOT="${QT_ROOT:-/opt/qt}"
LLVM_VERSION="${LLVM_VERSION:-20}"

apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates curl wget gnupg lsb-release software-properties-common \
  git python3 python3-venv cmake ninja-build pkg-config file zip unzip \
  patchelf xz-utils squashfs-tools desktop-file-utils \
  g++-13 libstdc++-13-dev \
  libx11-dev libx11-xcb-dev libxcb1-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
  libxkbcommon-x11-dev libwayland-dev wayland-protocols libdecor-0-dev \
  libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev \
  libfontconfig1 libfreetype6 libglib2.0-0 libdbus-1-3 libgl1 libegl1 \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
  libxkbcommon-x11-0 libwayland-cursor0 libwayland-egl1

# clang from apt.llvm.org. It builds against the newest installed GCC's
# libstdc++ (13 here); every target system's libstdc++ is at least that new.
curl -fsSL https://apt.llvm.org/llvm.sh -o /tmp/llvm.sh
bash /tmp/llvm.sh "$LLVM_VERSION"
clang++-"$LLVM_VERSION" --version | head -1

cmake --version | head -1
# The Qt installer, in a venv (24.04's pip refuses system-wide installs).
python3 -m venv /opt/aqt-venv
/opt/aqt-venv/bin/pip install --no-cache-dir --quiet aqtinstall

# Qt from the official binaries (built for glibc 2.28) rather than 24.04's
# Qt 6.4, matching the Windows build's Qt 6.8. qtwayland gives the launcher native Wayland support.
/opt/aqt-venv/bin/python -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT" \
  -m qtwayland 2>/dev/null ||
  /opt/aqt-venv/bin/python -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT"
ls "$QT_ROOT/$QT_VERSION/gcc_64/plugins/platforms"
