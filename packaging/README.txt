'SPLOSION MAN — Native Linux Port (ReXGlue recomp)
==================================================

WHAT THIS IS
------------
A native Linux build of 'Splosion Man (Xbox 360 / XBLA), recompiled from
the original game code with the ReXGlue SDK. It runs as a real Linux
program — no Xbox, no emulation layer, no Wine.

IMPORTANT: No game data is included. You must supply your own legally
ripped copy of the game (a folder containing default.xex and the game's
extracted files).

REQUIREMENTS
------------
- x86_64 Linux with a Vulkan-capable GPU (AMD/Intel/NVIDIA, recent Mesa
  or proprietary driver)
- glibc 2.39 or newer (Ubuntu 24.04+, or equivalent)
- A gamepad is recommended (Xbox-style controllers work out of the box,
  including rumble)

HOW TO RUN
----------
1. Extract the zip.
2. Make the AppImage executable:  chmod +x SplosionMan-x86_64.AppImage
3. Run it:                          ./SplosionMan-x86_64.AppImage
4. On first launch the launcher looks for your game folder automatically
   (next to the AppImage, in ~/Games, or on mounted drives under /mnt and
   /media). If it can't find one, it asks you to pick the folder that
   contains default.xex. Your choice is remembered.

SETTINGS (in the launcher)
--------------------------
- Upscaling: AMD FSR (default) renders the game's 720p output up to your
  display resolution with FidelityFX upscaling + sharpening. Bilinear and
  CAS are also available.
- Fullscreen, VSync, frame limit, FXAA (swap post effect), anisotropic
  filtering, resolution scale, and audio options are on the GRAPHICS and
  AUDIO tabs. Settings save automatically when you press Play.

SAVES
-----
Save data lives in the standard per-user folder:
  ~/.local/share/splosionman/
(Shader caches live there too — the first run may stutter briefly while
shaders compile; later runs are smooth.)

NOTES
-----
- The full game is unlocked by default (the launcher sets the license
  flag the game checks; your own copy, your own hardware).
- If the launcher window looks wrong on Wayland, try launching from a
  terminal so Qt picks your session's platform automatically.

Built with the ReXGlue SDK. This is a fan preservation project; it is not
affiliated with Twisted Pixel Games or Microsoft.
