'Splosion Man - Android Port (Test Build 1)
================================================

A native Android port of the 'Splosion Man recompilation (ReXGlue).
This is a FIRST TEST BUILD: it compiles and packages cleanly, but it has
not been run on a real device yet. Your test is the verdict - expect the
possibility of crashes, and see "If something goes wrong" below.

Requirements
------------
- Android 10 or newer (API 29+), arm64 device (any modern phone/tablet)
- ~500 MB free space (APK is ~37 MB; the imported game is ~400 MB)
- Your own copy of 'Splosion Man for Xbox 360 (XBLA). Game data is NOT
  included - the app imports it from your copy on first launch.

Install
-------
1. Copy SplosionMan-Android-Test1.apk to the device.
2. Tap it and allow "install from this source" when Android asks.
   (The build is debug-signed; that's expected for a sideloaded test.)

Loading the game (first launch)
-------------------------------
The app opens on a "Load Game Assets" menu. Pick whichever you have:

A) The original XBLA package file from your Xbox 360 content folder
   (the big file for title 5841098F - a few hundred MB).
   - Easiest: connect the phone over USB and copy the package into:
       Android/data/com.tp.splosionman/files/inbox/
     then relaunch (or tap Rescan), tap the package in the list, and
     the app extracts it for you. Takes a minute or two.
   - Or tap "Browse for XBLA package..." and pick it anywhere on the
     device.
   Note: a tiny ~6 MB file also named 5841098F... exists (theme/avatar
   content) - that is NOT the game. The wizard will reject it and say
   no playable game was found.

B) An already-extracted game folder (contains default.xex):
   copy the folder's contents into:
       Android/data/com.tp.splosionman/files/game/
   and relaunch; the game boots straight in.

After a successful import, later launches go straight to the game.

Controls (on-screen)
--------------------
- Left side: floating analog stick - touch and drag anywhere on the
  left of the screen. Pushing it far also presses the D-pad (menus).
- Right side: Y / X / B / A diamond (A = confirm, B = back in menus).
- Big orange SPLODE button = A. In game, every face button 'splodes,
  so SPLODE is your main button; A/B stay clean for menus.
- LB / RB near the top corners, START / BACK at the bottom.
- The small pill at the top center (HIDE / PAD) hides the controls.
- A physical Bluetooth/USB gamepad also works if you prefer it.

If something goes wrong
-----------------------
- The game writes a log you can actually reach, at:
    Android/data/com.tp.splosionman/files/logs/game.log
  Send that file back with any bug report.
- Or, with the phone plugged into a PC: adb logcat -s SDL,REXLOG
  (adb is on your Kubuntu box in the project android-sdk/platform-tools).
- If the import fails with a title-ID message, the picked file belongs
  to a different game. 'Splosion Man is title 5841098F.

Build notes
-----------
- arm64-v8a only, minSdk 29, targetSdk 35. Gradle 8.10.2 / AGP 8.7.3,
  NDK 28.2, SDL 3.5 + ReXGlue 0.10.0 (house-patched) + an Android patch
  set (Vulkan Android surface, AArch64 fiber backend, STFS importer,
  touch gamepad) developed for this port.
- The STFS extractor built into the app was verified byte-for-byte
  against known-good extractions on the PC before shipping.

Source layout (on the build machine): /mnt/sdb1/Games/SplosionMan-Android
  android/   - Gradle project + Android-only native sources
  game/      - SplosionManRecomp project (generated code, no XEX)
  sdk/       - ReXGlue SDK with the Android patch set applied
  sdk-patch/ - the patch set itself (apply_android_sdk_patches.py)
