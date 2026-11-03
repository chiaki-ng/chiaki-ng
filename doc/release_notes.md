## System Pre-requisites

### All Systems
- GPU or integrated graphics supporting Vulkan 1.2 or higher

### MacOS
- macOS 15 (Sequoia) or later on Apple Silicon (arm64)

### Linux Flatpak
- pipewire

### Linux Appimage
- libva
- pipewire

## Where to Download 

### Linux
- Flatpak Release on Flathub as io.github.streetpea.Chiaki4deck
- Appimage  release (arm64 and x86_64) attached

### MacOS
- Homebrew
  ```
  brew install --cask streetpea/streetpea/chiaki-ng
  ```
- Attached releases for Apple Silicon (arm64) only
- Intel macOS (x86_64/amd64) release, weekly, and manual builds are no longer produced, and pull requests are no longer tested on Intel macOS.

### Windows:
- winget
  ```
  winget install --id=StreetPea.chiaki-ng -e
  ```
- chocolatey
  ```
  choco install chiaki-ng
  ```
 - Attached releases (installer and portable)

## Switch:

Please see akira releases here: https://github.com/xlanor/akira/releases

## Additional Notes
Note: Pipewire required for appimage. For Linux, pipewire is required for DualSense haptics.
For flatpak, you can run with `--env=SDL_AUDIODRIVER=pulse` to use pulse instead of pipewire if you really want that (doing this means DualSense haptics won't work)

## Updates

### v1.10.1

This release provides the following improvements:

- fix controller buttons that could stop working or stay held mid-stream, and get adaptive trigger effects from more PS5 games
- improve stream stability: recover from a lost keyframe instead of freezing, keep PSN connections open through bad packets, resend more lost packets, and fix dropped frames after packet loss
- add direct frame mapping (on by default) along with frame pacing and renderer fixes, and fix crashes when a session ends or the video queue resets and a VRAM leak while the session is locked
- bring back Vulkan as the default renderer on macOS, which brings back HDR, with MoltenVK 1.4.2; the macOS app is now signed and notarized, so it opens without a security exception, and it no longer freezes when you quit during a stream
- keep DualSense rumble working after switching between Bluetooth and USB, start wired haptics as soon as the controller's audio device appears, and keep haptics in step after network stalls
- fix the microphone cutting out, mostly with quieter mics, and resend each mic frame in the next two packets
- fix the AppImage crashing at PSN login on distros with a different NSS version and make it use its own SDL3
- resend the wakeup while the console wakes, fix UPnP discovery, connecting by ports and renaming consoles, and recover incomplete certificate chains for PSN
- stop writing the login PIN to the session log
- update libplacebo, SDL, curl and other libraries, move every package to FFmpeg 9.0.2, and move the Flatpak to KDE runtime 6.11 and the AppImages to Qt 6.9.3

