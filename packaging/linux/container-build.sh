#!/usr/bin/env bash
# Run the CI Linux build locally in an almalinux:9 container (podman or
# docker), on a clean clone of this repo's HEAD plus uncommitted packaging
# changes. Results land in out/ci-container/. Your own out/build is untouched.
#
# Env: IMAGE  an image that already has deps-almalinux9.sh applied (skips the
#             dependency install; e.g. one saved with `podman commit`)
#      JOBS   parallel build jobs (default: nproc)
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
if [[ -n "${IMAGE:-}" ]]; then
  deps=':'
else
  IMAGE=docker.io/library/almalinux:9
  deps='packaging/linux/deps-almalinux9.sh > /work/deps.log 2>&1 || { tail -40 /work/deps.log; exit 1; }'
fi
"$ENGINE" run --rm -v "$DEST:/work:Z" -w /work/src \
  -e WORK=/work/build -e VERSION="${VERSION:-local}" -e JOBS="${JOBS:-$(nproc)}" \
  "$IMAGE" \
  bash -c "$deps
           packaging/linux/build-release.sh"
ls -la "$DEST"/build/*.AppImage
