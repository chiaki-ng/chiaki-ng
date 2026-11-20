#!/bin/bash
# Sign chiaki-ng.app inside out with a Developer ID identity and the hardened runtime, so it can be notarized.
# Usage: macos-sign.sh <path/to/chiaki-ng.app> <identity>
set -euo pipefail

app="$1"
identity="$2"
script_dir="$(cd "$(dirname "$0")" && pwd)"
app_entitlements="$script_dir/../gui/entitlements.xml"
helper_entitlements="$script_dir/macos/QtWebEngineProcess.entitlements"

sign() {
	codesign --force --timestamp --options runtime --sign "$identity" "$@"
}

# Every loose Mach-O library: Qt plugins, QML plugins, FFmpeg and other dylibs, MoltenVK in Resources
find "$app/Contents" -type f \( -name "*.dylib" -o -name "*.so" \) -print0 | while IFS= read -r -d '' library; do
	sign "$library"
done

# The WebEngine helper is a nested app and has to be signed before the framework that holds it
find "$app/Contents/Frameworks" -type d -name "*.app" -path "*/Helpers/*" -print0 | while IFS= read -r -d '' helper; do
	sign --entitlements "$helper_entitlements" "$helper"
done

# Frameworks, each signed as a bundle
find "$app/Contents/Frameworks" -maxdepth 1 -type d -name "*.framework" -print0 | while IFS= read -r -d '' framework; do
	sign "$framework"
done

# The app itself last
sign --entitlements "$app_entitlements" "$app"

codesign --verify --deep --strict --verbose=2 "$app"
