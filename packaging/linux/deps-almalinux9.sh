#!/usr/bin/env bash
# Install the build dependencies for the portable Linux build on AlmaLinux 9
# (glibc 2.34). Run as root inside an almalinux:9 container; used by
# .github/workflows/linux.yml and by packaging/linux/container-build.sh.
#
# Why AlmaLinux 9: binaries only run on systems with a glibc at least as new
# as the one they were built against. 2.34 covers Ubuntu 22.04+ (the oldest
# Ubuntu LTS in standard support, which the AppImage catalog tests on),
# Debian 12+, Fedora 35+, RHEL/Rocky/Alma 9+, the Steam Deck and Arch.
#
# The SDK needs a newer C++ library than the system's (GCC 11). gcc-toolset-13
# solves that the way Red Hat (and Python's manylinux wheels) do: its
# libstdc++ links everything newer than GCC 11 statically into each binary
# (libstdc++_nonshared.a), so the result needs only the system libstdc++ that
# every target already has. clang uses it through --gcc-install-dir (see
# build-release.sh); nothing is bundled and nothing is switched at run time.
set -euo pipefail

QT_VERSION="${QT_VERSION:-6.8.3}"
QT_ROOT="${QT_ROOT:-/opt/qt}"
GCC_TOOLSET="${GCC_TOOLSET:-13}"
# The SDK needs CMake 3.25+. Stay below 4.0, which drops compatibility with
# the old cmake_minimum_required of some submodules.
CMAKE_PIP_SPEC="${CMAKE_PIP_SPEC:-cmake>=3.28,<4}"

dnf -y install dnf-plugins-core epel-release
dnf config-manager --set-enabled crb
dnf -y install --setopt=install_weak_deps=False \
  git ca-certificates findutils which file zip unzip xz binutils \
  python3 python3-pip ninja-build pkgconf-pkg-config patchelf squashfs-tools \
  desktop-file-utils \
  clang lld "gcc-toolset-$GCC_TOOLSET-gcc-c++" \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXi-devel \
  libXScrnSaver-devel libXfixes-devel libXtst-devel libxcb-devel \
  libxkbcommon-devel libxkbcommon-x11-devel wayland-devel wayland-protocols-devel \
  libdecor-devel libglvnd-devel mesa-libEGL-devel mesa-libGL-devel \
  libdrm-devel mesa-libgbm-devel \
  alsa-lib-devel pulseaudio-libs-devel pipewire-devel dbus-devel systemd-devel \
  fontconfig freetype glib2 dbus-libs \
  xcb-util-cursor xcb-util-wm xcb-util-image xcb-util-keysyms \
  xcb-util-renderutil libxkbcommon-x11 libwayland-cursor libwayland-egl
clang --version | head -1
"/opt/rh/gcc-toolset-$GCC_TOOLSET/root/usr/bin/g++" --version | head -1

# CMake and the Qt installer, in a venv.
python3 -m venv /opt/build-venv
/opt/build-venv/bin/pip install --no-cache-dir --quiet --upgrade pip
/opt/build-venv/bin/pip install --no-cache-dir --quiet "$CMAKE_PIP_SPEC" aqtinstall
ln -sf /opt/build-venv/bin/cmake /usr/local/bin/cmake
ln -sf /opt/build-venv/bin/ctest /usr/local/bin/ctest
cmake --version | head -1

# Qt from the official binaries (built for glibc 2.28) rather than the
# distro's, matching the Windows build's Qt 6.8. qtwayland gives the launcher
# native Wayland support.
/opt/build-venv/bin/python -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT" \
  -m qtwayland 2>/dev/null ||
  /opt/build-venv/bin/python -m aqt install-qt linux desktop "$QT_VERSION" linux_gcc_64 -O "$QT_ROOT"
ls "$QT_ROOT/$QT_VERSION/gcc_64/plugins/platforms"
