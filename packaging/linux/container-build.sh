#!/usr/bin/env bash
# Run the CI Linux build locally in an ubuntu:24.04 container (podman or
# docker), on a clean clone of this repo's HEAD plus uncommitted packaging
# changes. Results land in out/ci-container/. Your own out/build is untouched.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENGINE="${ENGINE:-$(command -v podman || command -v docker)}"
DEST="$ROOT/out/ci-container"
SRC="$DEST/src"
mkdir -p "$DEST"
rm -rf "$SRC"
git clone --quiet --no-local "$ROOT" "$SRC"
# Carry over the working-tree versions of the packaging and workflow files.
(cd "$ROOT" && git ls-files -mo --exclude-standard -- packaging .github | while read -r f; do
  mkdir -p "$SRC/$(dirname "$f")"; [[ -e "$f" ]] && cp -a "$f" "$SRC/$f"; done)
"$ENGINE" run --rm -v "$DEST:/work:Z" -w /work/src \
  -e WORK=/work/build -e VERSION="${VERSION:-local}" -e JOBS="${JOBS:-$(nproc)}" \
  docker.io/library/ubuntu:24.04 \
  bash -c 'packaging/linux/deps-ubuntu2404.sh > /work/deps.log 2>&1 || { tail -40 /work/deps.log; exit 1; }
           packaging/linux/build-release.sh'
ls -la "$DEST"/build/*.AppImage
