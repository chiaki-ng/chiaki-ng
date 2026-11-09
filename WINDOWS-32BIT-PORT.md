# Windows 32-bit GUI port: scope and verification

## Status

**Not implemented or verified yet.** The current GUI build uses Qt 6 (`gui/CMakeLists.txt`), and the existing Windows workflow builds x64. This branch is intended to investigate a native x86 GUI build; it does not currently provide a usable 32-bit Windows package.

## Port work required

1. Select and validate a Windows x86 toolchain and a Qt version/build with the required GUI modules. The current Qt 6 dependency cannot simply be switched to an x86 triplet; determine whether the code can be ported to a 32-bit-capable Qt release or whether a supported Qt 6 x86 distribution exists.
2. Audit the GUI and shared code for Qt-version-specific APIs before choosing a Qt migration strategy. Preserve the existing Qt 6 build for supported platforms.
3. Build every native dependency for x86, including the media/FFmpeg and libplacebo stack, SDL, OpenSSL, protobuf, and the libraries provided through vcpkg or submodules. Do not mix x86 and x64 binaries.
4. Add a dedicated Windows x86 CI workflow with explicit architecture assertions for the compiler, libraries, executable, and packaged DLLs.
5. Package and test on a 32-bit Windows installation (or a supported 32-bit Windows runtime environment), including controller input, streaming, audio/video decode, installer behavior, and launch/runtime DLL resolution.
6. Publish a release only after CI and runtime verification pass; update this document and the README with supported Windows versions and known limitations.

## Acceptance criteria

- CI produces a GUI executable identified as PE32 / x86 (not PE32+ / x64).
- All bundled native libraries and installer outputs are 32-bit and load successfully on a 32-bit Windows device.
- The app launches and core Remote Play scenarios are exercised on an actual or appropriately configured 32-bit Windows environment.
- The regular upstream/x64 Windows workflow remains unaffected.

## Current build context

- Existing Windows build workflow: `.github/workflows/build-windows-x86.yml` (x64/VC).
- GUI Qt components and optional WebEngine are declared in `gui/CMakeLists.txt`.
- Installer architecture restrictions are in `scripts/chiaki-ng.iss`.

This is a porting checklist, not a promise of compatibility or an installation guide. Do not label an untested build as a 32-bit release.
