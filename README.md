# 'Splosion Man — Linux Recompilation

A native Linux port of the Xbox 360 XBLA game **'Splosion Man** (Twisted Pixel, 2009),
built with the [ReXGlue SDK](https://github.com/ReXGlue/rexglue-sdk) static
recompiler — the same toolchain family as
[Dante's Inferno](https://github.com/thefixinhixon/hells-gate-recomp) and
[Condemned 2](https://github.com/thefixinhixon/Condemned2Recomp).

**Status: fully playable** — graphics, PipeWire audio, controller + vibration,
full-game unlock, FSR upscaling, verified on Kubuntu (AMD RX 6600 / RADV).

> **No game data is included in this repository or its releases.**
> You must supply your own legally obtained copy of the game: a folder
> containing `default.xex` (e.g. extracted from your Xbox 360 disc/download).
> 'Splosion Man is © Twisted Pixel Games / Microsoft. This is a
> fan-made interoperability project; do not redistribute game assets.

## Download

Grab the latest release (AppImage, x86_64). Extract, `chmod +x`, run —
the launcher auto-detects your game folder (next to the AppImage,
`~/Games`, mounted drives) or lets you pick it. Requirements: a distro
with glibc 2.38+ (Ubuntu 23.10 / Debian 13 / Fedora 39 or newer),
Vulkan drivers for your GPU, and PipeWire or PulseAudio.

## The launcher

Qt6 launcher (PLAY / GRAPHICS / AUDIO / LANGUAGE tabs):

- **FSR upscaling** (on by default), resolution scale 720p→4K, FXAA
- Fullscreen / VSync toggles, frame limit, anisotropic filtering
- Settings persist; logs land in the save-data folder

The launcher applies the flags the game needs:
`--gpu_plugin=xenos --license_mask=1 --vulkan_async_skip_incomplete_frames=false
--vulkan_sparse_shared_memory=false --log_file=<save-data>/logs/game.log`

## Building from source

You need: the ReXGlue SDK (with FidelityFX enabled), clang 20, Qt6 dev,
Vulkan headers, and **PipeWire + SPA development headers** — without
them SDL silently builds *without* a PipeWire audio backend and the
game has no sound on PipeWire systems. (Ask us how we know.)

1. Build the ReXGlue SDK (`REXGLUE_ENABLE_FIDELITYFX=ON`).
2. Codegen from your own `default.xex` (or use the included `generated/`):
   `rexglue codegen`, then `scripts/fix-labels.py` and
   `scripts/smart-patch.py` (host setjmp/longjmp registry, branch fixes).
3. Build the game (CMake preset `linux-amd64`), the SDK runtime libs,
   and `launcher-linux/` (CMake, Qt6).
4. Package with `packaging/appimage/build-appimage-x86_64.sh`.

Note: **never mix** the executable and `librexruntime.so` /
`librexgpu-xenos.so` from different build sets — it corrupts the heap.

## Known landmine: `/dev/shm` leaks

The runtime backs guest memory with a ~4.5 GB `xenia_memory_*` file in
`/dev/shm` per run. Crashed or killed runs **leak** these files; when
`/dev/shm` fills, new runs die with a bus error immediately after
"Guest memory arena mapped" (launcher shows exit code 7). If the game
suddenly stops launching, check `df -h /dev/shm` and remove stale
files: `rm /dev/shm/xenia_memory_*`.

Also: a crash while the shader cache writes can poison it — if the
game starts freezing at the intro→3D transition, delete
`~/.local/share/splosionman/cache` (your saves live elsewhere and are safe).

## Credits & disclosure

- ReXGlue SDK team — the recompiler and runtime that make this possible.
- Twisted Pixel Games — for a game absolutely worth this much trouble.
- This port was built by Jason Hixon with heavy AI assistance
  (Muse, by Meta) on the coding side, and tested the old-fashioned
  way: by playing it. The bugs were real, the explosions earned.
