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
- **Windows: test build available** (in the latest release) —
  built by CI, tested under Wine/Proton. Native Windows reports welcome.

> **No game data is included in this repository or its releases.**
> You must supply your own legally obtained copy of the game — the
> launchers can import your XBLA package directly, or use a folder
> containing `default.xex` (e.g. extracted from your Xbox 360
> disc/download). 'Splosion Man is © Twisted Pixel Games / Microsoft.
> This is a fan-made interoperability project; do not redistribute
> game assets.

## Download

### Linux

Grab the AppImage (x86_64) from the latest release. `chmod +x`, run —
the launcher imports your XBLA package on the Game tab, or lets you
pick an extracted game folder. Requirements:

- **glibc 2.34 or newer**: Ubuntu 22.04+ (Mint 21+, Pop!_OS 22.04+),
  Debian 12+, Fedora 35+, RHEL / Rocky / AlmaLinux 9+, SteamOS (Steam
  Deck), Arch. Nothing else from your system's C/C++ libraries is newer
  than what those ship.
- Vulkan drivers for your GPU.
- PipeWire or PulseAudio (the game needs an audio output device).

The AppImage carries update information: AppImageUpdate, Gear Lever,
AppImageLauncher and similar tools can update it in place from the
latest GitHub release (only the changed parts are downloaded).

### Windows (test build)

Grab `SplosionMan-Windows-<version>.zip` from the latest release. Extract
it into a new, empty folder, run `SplosionManRecomp.exe`
(`TPSplosionmanLauncher.exe` up to v2.2.2), import your XBLA package, and
press Play. Requirements: Windows
10/11 64-bit, a GPU with DirectX 12 support (the Windows build renders
with Direct3D 12), the Microsoft Visual C++ Redistributable (x64), and
an audio output device. The four game program files (`splosionman.exe`,
`rexruntime.dll`, `rexgpu-xenos.dll`, `amd_fidelityfx_dx12.dll`) must
stay together in the folder — the launcher checks for them at startup.

## The launcher

Both platforms ship the **SplosionManRecomp** launcher (Qt 6, `launcher/`),
written for this project:

- **Import XBLA package**: reads your package directly (built-in STFS
  reader, no other tools) and extracts the game. Or choose a folder that
  already holds the extracted game.
- **Import DLC**: installs a 'Splosion Man DLC package (Prologue to Ms.
  'Splosion Man) where the game looks for downloadable content, in its save
  folder. Title updates are not needed and are not installed.
- **The game's own art.** The launcher ships none: it shows a drawn
  placeholder until the game data is there, then makes its banner and
  icon from the dashboard art inside your package.
- **Settings** are the same options as the in-game menu, with the same
  choices and a plain-language explanation for each, plus game language,
  detailed logging and the save folder. They save as you change them; a
  value changed in the launcher replaces one chosen earlier in the
  in-game menu.
- **Light and dark**, following your desktop (on Linux through the XDG
  portal, so it also works inside the AppImage) or set by hand.
- **Open logs folder** for bug reports: the game writes `game.log` and the
  launcher `last-command.txt` (the exact command) there.
  `SplosionManRecomp --print-command` prints that command without
  starting the game.
- Settings from the old launcher (up to v2.2.2) carry over on first start.

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
   libs, and `launcher/` (CMake, Qt 6.5 or newer).
4. Package with `packaging/appimage/build-appimage-x86_64.sh`.

Release AppImages are built by the **Linux build** GitHub Actions workflow
inside an AlmaLinux 9 container, so they need only glibc 2.34 and the
system's GCC 11 libstdc++ (Ubuntu 22.04+, Debian 12+, Fedora 35+, RHEL 9
family, Steam Deck, Arch). The C++ library pieces the SDK needs beyond
GCC 11 come from gcc-toolset-13, which links them into each binary; the
packaging step fails if anything shipped needs a newer glibc or
libstdc++. A build made directly on a newer distro needs that distro's
glibc. To reproduce the release build locally with podman or docker:
`packaging/linux/container-build.sh` (output in `out/ci-container/`).

Note: **never mix** the executable and runtime libraries from
different build sets — it corrupts the heap.

### Windows

Windows builds are produced by GitHub Actions
(`.github/workflows/windows.yml`): the workflow clones the pinned
ReXGlue SDK, applies `patches/house-sdk.patch`, builds the game with
the `win-amd64-release` preset, and builds `launcher/` against
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

## License

This project's own code — the launcher (`launcher/`), the game glue in
`src/`, the Android port, packaging, scripts and build files — is released
under the [MIT License](LICENSE).

Not covered by it:

- **The game.** 'Splosion Man's code, data, art, audio, characters and
  names are © Twisted Pixel Games / Microsoft. `generated/` is
  machine-translated from the game's executable and is not offered under
  the MIT License. The launcher ships no game art; it shows art from your
  own game data.
- **The ReXGlue SDK** and `patches/house-sdk.patch` (this project's
  changes to it) are under the SDK's own BSD-3-Clause license (derived
  from Xenia). Release packages include that license.
- **Third-party components** in the releases (Qt, SDL, AMD FidelityFX
  and others) keep their own licenses.

## Credits & disclosure

- ReXGlue SDK team — the recompiler and runtime that make this
  possible.
- Launchers up to v2.2.2 were adapted from the **Linux Qt launcher
  written by MaSieS4Fun** for
  [hells-gate-recomp](https://github.com/florinp93/hells-gate-recomp),
  Zerkiller's (florinp93's) Dante's Inferno recompilation, whose
  lineage goes back to Zerkiller's original launcher for that project.
  The current launcher is a new codebase, but the idea of a friendly
  launcher with explained settings and a built-in package import
  started there. Credit where it's due: thank you both.
- Twisted Pixel Games — for a game absolutely worth this much trouble.
- This port was built by Jason Hixon with heavy AI assistance
  (Muse, by Meta) on the coding side, and tested the old-fashioned
  way: by playing it. The bugs were real, the explosions earned.
