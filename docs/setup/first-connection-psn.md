# First registration over PSN (experimental branch)

This branch enables first registration from outside the console's local network.
The implementation has been built for Windows x64 with MSYS2 UCRT64 and passed
the checks below. It has not been tested against real PS4/PS5 consoles; treat
the first-registration feature as experimental until that validation is done.

## Requirements and scope

- Sign into the PSN account used by the console.
- Enable Remote Play on the console beforehand. To wake it remotely, enable the
  console's network/Remote Play rest-mode settings and leave it in rest mode.
- PS5 devices must be returned by PSN with Remote Play enabled.
- PS4 connections target the account's **main PS4 only**, as in upstream chiaki-ng.
  The generic Main PS4 Console card is an entry point, not proof that PSN found a
  PS4. It can appear for accounts without a PS4; connection then fails normally.
- Existing PSN login, firmware and NAT/UDP hole-punching requirements still apply.
  This change does not add a relay or enable Remote Play on an unconfigured console.

## Usage after building

1. Use a new chiaki-ng profile with no registered consoles, on a different network
   from the console, to test the first-use path.
2. Open Settings -> Config -> Login to PSN and finish the normal login flow.
3. Refresh PSN Hosts if necessary. Choose the PS5 or Main PS4 Console card marked
   "Register via PSN (no local connection needed)".
4. Accept registration. No local IP address or eight-digit pairing PIN is needed.
5. After registration finishes, select the console again to start streaming.
   A configured console **login PIN** is separate and may still be required.

If a PS4 is already registered, the generic main-PS4 entry can start streaming
directly. A successful PSN handshake saves the actual console's registration,
even when that console differs from the PS4 registered earlier.

## Implementation

- Include unregistered PS5s and the main-PS4 entry in PSN host discovery results.
- Share the visible PSN host list between rendering and selection.
- Select the remote console's target type for automatic registration and retain
  the local PS4 identity check only when a specific local console was selected.
- Save registration results for ordinary PSN streaming handshakes as well as
  registration-only sessions, preserving any saved console login PIN.
- Remove the old PS4 check requiring a locally saved nickname before streaming.

The existing protocol already obtains fresh registration keys over the PSN
connection. This branch reuses that authenticated path; it does not bypass PSN
authentication or change registration cryptography.

## Required validation before release

The dependency-free UI routing regression check can be run with
`node test/psn-registration-ui.cjs`. It executes the production QML registration
handler with UI boundary doubles (10 cases), not the Qt application or protocol.
It and `git diff --check` pass for this source change. Windows Release compilation
also passed with GCC 16.2, Qt 6.12, FFmpeg 9.1 and libplacebo 7.387.0. The native
unit suite reports 122 passed and one platform-specific test skipped. QML parsing,
a recursive check of all 198 packaged EXE/DLL dependencies, and a packaged
`chiaki.exe --help` launch with a clean system-only PATH passed. This startup
check exits before reading application settings; it is not a full GUI or PSN
connection test. All real-console scenarios below remain unverified.

| Scenario | Expected result |
| --- | --- |
| Fresh profile, remote PS5 | PS5 is listed; remote registration and subsequent stream succeed |
| Fresh profile, main PS4 | PS4 entry remains visible with zero registrations; registration succeeds |
| Only a different PS4 was registered earlier | Main PS4 connects and its registration is saved |
| Saved console login PIN | Reconnecting keeps the saved PIN |
| Locally discovered non-main PS4 | Automatic registration still rejects a different main PS4 |
| Multiple remote consoles, one local or waking | Clicking a visible card selects that same console |
| Registration declined | Returns to the list without an empty manual-registration dialog |
| Expired PSN token / failed NAT traversal / cancellation | Existing error or cancellation flow; no false success |
| Return home after remote registration | Local connection can reuse the saved registration |

## Windows build used for validation

Initialize the desktop dependencies (`third-party/nanopb`, `third-party/jerasure`,
`third-party/gf-complete`, `third-party/curl`, `third-party/cpp-steam-tools`, and
`test/munit`) with `git submodule update --init --recursive`. In an MSYS2 UCRT64
shell with the upstream build dependencies installed:

```sh
cmake -S . -B build-psn -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCHIAKI_ENABLE_CLI=OFF \
  -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF -DCHIAKI_USE_SYSTEM_CURL=OFF \
  -DCHIAKI_ENABLE_TESTS=ON \
  -DPython_EXECUTABLE="$(cygpath -m /ucrt64/bin/python3.exe)" \
  -DPYTHON_EXECUTABLE="$(cygpath -m /ucrt64/bin/python3.exe)"
cmake --build build-psn --parallel 12 --target chiaki chiaki-unit
ctest --test-dir build-psn --output-on-failure
bash scripts/deploy-windows-msys2.sh dist/chiaki-ng-psn-windows \
  build-psn/gui/chiaki.exe "$PWD/build-psn/third-party/cpp-steam-tools" \
  /ucrt64 gui/src/qml
```

This MinGW build uses the existing external-browser PSN login fallback because
Qt WebEngine is unavailable in this toolchain. Keep the complete deployed
directory together; copying `chiaki.exe` by itself is insufficient. Preserve
bundled curl to retain the existing Windows Schannel certificate-chain handling.
