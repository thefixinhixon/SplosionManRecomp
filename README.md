# 'Splosion Man — Linux & Windows Recompilation

Native **Linux and Windows** ports of the Xbox 360 XBLA game
**'Splosion Man** (Twisted Pixel, 2009), built with the
[ReXGlue SDK](https://github.com/ReXGlue/rexglue-sdk) static
recompiler — the same toolchain family as
[Dante's Inferno](https://github.com/thefixinhixon/hells-gate-recomp) and
[Condemned 2](https://github.com/thefixinhixon/Condemned2Recomp).

**Status:**

- **Linux: fully playable** — graphics, PipeWire audio, controller +
  vibration, full-game unlock, FSR upscaling, verified on Kubuntu
  (AMD RX 6600 / RADV).
- **Windows: test build available** (see the `windows-test-1`
  pre-release) — built by CI, verified end-to-end via Proton (levels,
  sound, gamepad + vibration, FSR). Native Windows reports welcome.

> **No game data is included in this repository or its releases.**
> You must supply your own legally obtained copy of the game — the
> launchers can import your XBLA package directly, or use a folder
> containing `default.xex` (e.g. extracted from your Xbox 360
> disc/download). 'Splosion Man is © Twisted Pixel Games / Microsoft.
> This is a fan-made interoperability project; do not redistribute
> game assets.

## Download

### Linux — release v2.0

Grab the AppImage (x86_64) from the latest release. `chmod +x`, run —
the launcher imports your XBLA package on the Game tab, or lets you
pick an extracted game folder. Requirements: a distro with
**glibc 2.43+** (this build was produced on Ubuntu 26.04; a more
portable build is planned), Vulkan drivers for your GPU, and PipeWire
or PulseAudio.

### Windows — pre-release `windows-test-1`

Grab `SplosionMan-Windows-Test-v0.1.zip` from the pre-release.
Extract the folder anywhere, run `TPSplosionmanLauncher.exe`, import
your XBLA package on the Game tab, and press Play. Requirements:
Windows 10/11 64-bit and a GPU with Vulkan support. The four game
program files (`splosionman.exe`, `rexruntime.dll`, `rexgpu-xenos.dll`,
`amd_fidelityfx_dx12.dll`) must stay together in the folder — the
launcher checks for them at startup.

## The launcher

Both platforms ship the **TP 'Splosion Man Launcher** (Qt6, theme
included), from the shared launcher codebase in `tp-launcher/`:

- **Import XBLA package** on the Game tab — built-in STFS extraction,
  no external tools needed
- **23 settings, each with a plain-language explanation**: internal
  resolution (720p→4K), **AMD FSR upscaling** (on by default), FXAA,
  MSAA, anisotropic filtering, VSync, frame limit, audio options,
  storage locations, and advanced Vulkan/profile options
- Settings save automatically as you change them; Reset to Defaults
  included
- On Linux, the launcher also vacuums leaked guest-memory mappings
  from crashed runs before launching

## Building from source

### Linux

You need: the ReXGlue SDK (with FidelityFX enabled), clang 20, Qt6
dev, Vulkan headers, and **PipeWire + SPA development headers** —
without them SDL silently builds *without* a PipeWire audio backend
and the game has no sound on PipeWire systems. (Ask us how we know.)

1. Build the ReXGlue SDK (`REXGLUE_ENABLE_FIDELITYFX=ON`).
2. Codegen from your own `default.xex` (or use the included
   `generated/`): `rexglue codegen`, then `scripts/fix-labels.py` and
   `scripts/smart-patch.py` (host setjmp/longjmp registry, branch
   fixes).
3. Build the game (CMake preset `linux-amd64`), the SDK runtime
   libs, and `tp-launcher/` (CMake, `-DTP_GAME=splosionman`).
4. Package with `packaging/appimage/build-appimage-x86_64.sh`.

Note: **never mix** the executable and runtime libraries from
different build sets — it corrupts the heap.

### Windows

Windows builds are produced by GitHub Actions
(`.github/workflows/windows.yml`): the workflow clones the pinned
ReXGlue SDK, applies `patches/house-sdk.patch`, builds the game with
the `win-amd64-release` preset, and builds `tp-launcher/` against
Qt 6.8 (MSVC). Trigger it from the Actions tab. The same steps work
in a local Windows checkout with Visual Studio 2022 and Qt 6.8
installed.

## Known landmine (Linux): `/dev/shm` leaks

The runtime backs guest memory with a ~4.5 GB `xenia_memory_*` file
in `/dev/shm` per run. Crashed or killed runs **leak** these files;
when `/dev/shm` fills, new runs die with a bus error immediately
after "Guest memory arena mapped" (launcher shows exit code 7). The
launcher now cleans these up automatically before launching; if the
game ever still refuses to start, check `df -h /dev/shm` and remove
stale files: `rm /dev/shm/xenia_memory_*`. (Windows uses named
page-file mappings that die with the process, so it doesn't have
this problem.)

Also, on either platform: a crash while the shader cache writes can
poison it — if the game starts freezing at the intro→3D transition,
delete the cache subfolder in the game's data folder (your saves live
elsewhere and are safe).

## Credits & disclosure

- ReXGlue SDK team — the recompiler and runtime that make this
  possible.
- The original launcher was adapted from the **Linux Qt launcher
  written by MaSieS4Fun** for
  [hells-gate-recomp](https://github.com/florinp93/hells-gate-recomp),
  Zerkiller's (florinp93's) Dante's Inferno recompilation — its
  settings system, game-folder setup, and AppImage packaging approach
  all started there, and the launcher lineage goes back to Zerkiller's
  original launcher for that project. The current TP launcher grew
  out of that lineage into a bespoke cross-platform launcher for this
  project family. Credit where it's due: thank you both.
- Twisted Pixel Games — for a game absolutely worth this much trouble.
- This port was built by Jason Hixon with heavy AI assistance
  (Muse, by Meta) on the coding side, and tested the old-fashioned
  way: by playing it. The bugs were real, the explosions earned.
