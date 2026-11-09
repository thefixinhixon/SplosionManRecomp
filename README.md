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

## In-game options menu

Press **F1** (keyboard) or **click both sticks (L3 + R3)** on a controller
to open the recomp options over the game. The game pauses behind it
(optional). D-pad / left stick or arrow keys move, Left / Right change a
value, LB / RB (Q / E) switch pages, B / Esc closes, Y / R twice resets.
The first page, **Game**, has Resume and **Quit game** (press twice to
confirm; the options are saved first).

| Page | Options |
|---|---|
| Performance | frame rate limit (30–360 or unlimited), VSync (off / low-latency / standard), frame queue depth, performance overlay (FPS or detailed), pause-while-open, fullscreen/windowed |
| Graphics | upscaling (bilinear / AMD FSR 1 / AMD CAS) + sharpness, FXAA, internal resolution, native 2x MSAA, anisotropic filtering |
| Audio | master volume (0–200%), mute, force stereo |
| Controls | vibration strength, keyboard-as-controller |

Everything except internal resolution, MSAA and force-stereo applies
instantly. Choices are saved to `recomp_options.toml` in the game's data
folder and override the launcher, **except** for a setting you later
change in the launcher: then the launcher's new value wins.

## Performance and latency

'Splosion Man simulates with real elapsed time (its frame delta comes from
the CPU timebase and is clamped), so it plays at the correct speed at any
frame rate. This port is built around that:

- **Unlocked frame rate.** The guest's vblank wait no longer caps the game
  at 60 (or 30 — with the old defaults it could drop to exactly 30 once a
  frame's CPU + GPU-thread work crossed one 16.6 ms vblank). A 2-second
  run measured the same distance at 30 and ~90 fps.
- **Low-latency frame pacing.** The frame limiter runs on the game thread
  in `VdSwap`, *before* the next frame reads the controller, instead of
  on the GPU thread after the frame was already simulated. Pacing is
  deadline-based with a short spin, so a 60 fps cap gives 16.67 ms frames
  (worst frame 17.2 ms measured).
- **Bounded frame queue.** The game may run at most one frame ahead of the
  GPU thread by default (`gpu_max_frames_ahead`), one frame less queued
  input than before, at the same throughput.
- **Low-latency VSync.** VSync uses mailbox presentation when the driver
  offers it: no tearing, no display-queue lag, uncapped rendering.
- **Less CPU waste.** Event-driven wakeups for multi-object waits (they
  used to poll every 1 ms), no per-register `switch` lookup on the GPU
  thread, bounded fence polling, and rate-limited logging for a draw the
  game issues every frame with an unset vertex fetch constant.

Diagnostics: `--perf_log_interval=2` logs fps, frame time, CPU work, GPU
wait and limiter time every 2 seconds.

## Building from source

### Linux

You need: the ReXGlue SDK (with FidelityFX enabled), clang 20, Qt6
dev, Vulkan headers, and **PipeWire + SPA development headers** —
without them SDL silently builds *without* a PipeWire audio backend
and the game has no sound on PipeWire systems. (Ask us how we know.)

1. Check out the ReXGlue SDK at the commit pinned in
   `.github/workflows/windows.yml` (`REXSDK_COMMIT`) and apply
   `patches/house-sdk.patch` (`git apply --whitespace=nowarn`). All of
   this port's runtime changes (pacing, menu hooks, perf fixes) are in
   that patch. Build with `REXGLUE_ENABLE_FIDELITYFX=ON`.
2. Codegen from your own `default.xex` (or use the included
   `generated/`): `rexglue codegen`, then `scripts/fix-labels.py` and
   `scripts/smart-patch.py` (host setjmp/longjmp registry, branch
   fixes).
3. Build the game (CMake preset `linux-amd64-release`, with
   `-DREXSDK_DIR=<sdk> -DREXGLUE_ENABLE_FIDELITYFX=ON`), the SDK runtime
   libs, and `tp-launcher/` (CMake, `-DTP_GAME=splosionman`).
4. Package with `packaging/appimage/build-appimage-x86_64.sh`.

Release AppImages are built by the **Linux build** GitHub Actions workflow
inside an Ubuntu 24.04 container, so they need only glibc 2.38 or newer
(Steam Deck / SteamOS 3.7, Ubuntu 24.04+, Fedora 40+, Debian 13+, Arch).
A build made directly on a newer distro needs that distro's glibc. To
reproduce the release build locally with podman or docker:
`packaging/linux/container-build.sh` (output in `out/ci-container/`).

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

## Fixed (Linux): `/dev/shm` leaks

The runtime backs guest memory with a ~4.8 GB `xenia_memory_*` file in
`/dev/shm`. Older builds left that file behind after **every** run, not just
crashes: closing the window hard-exits without running the cleanup. Once
`/dev/shm` filled, new runs died with a bus error right after "Guest memory
arena mapped" (the launcher showed exit code 7). The runtime now removes the
file's name as soon as it is created, so the kernel frees the memory whenever
the process ends, even after a crash or kill. Files left by older builds can
be removed with `rm /dev/shm/xenia_memory_*` while the game isn't running
(the launcher also does this before launching). Windows uses page-file
mappings that die with the process, so it never had this problem.

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
