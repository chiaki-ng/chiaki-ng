#!/usr/bin/env bash
# Builds chiaki-ng locally on macOS arm64 and packages it as a DMG.
# Run from the repo root: bash scripts/build-macos-local.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> Installing Homebrew dependencies"
brew update
brew uninstall pkgconfig 2>/dev/null || true
brew install qt@6 ffmpeg pkgconfig opus openssl@3 cmake ninja nasm \
    protobuf@29 speexdsp libplacebo wget python-setuptools json-c miniupnpc libevent
brew unlink openssl@4 2>/dev/null || true

echo "==> Installing Python protobuf"
pip3 install --user 'protobuf>=5,<6' --break-system-packages

echo "==> Building SDL3 + sdl2-compat"
SDL_BUILD_DIR="$REPO_ROOT/_sdl_build"
mkdir -p "$SDL_BUILD_DIR"
(
    cd "$SDL_BUILD_DIR"
    INSTALL_PREFIX=/usr/local bash "$REPO_ROOT/scripts/build-sdl2-compat.sh" "$SDL_BUILD_DIR"
)
sudo cp /usr/local/lib/pkgconfig/sdl2-compat.pc /usr/local/lib/pkgconfig/sdl2.pc

echo "==> Configuring chiaki-ng"
BREW="$(brew --prefix)"
cmake -S . -B build -G "Ninja" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCHIAKI_ENABLE_CLI=OFF \
    -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF \
    -DOPENSSL_ROOT_DIR="$BREW/opt/openssl@3" \
    -DOPENSSL_INCLUDE_DIR="$BREW/opt/openssl@3/include" \
    -DOPENSSL_CRYPTO_LIBRARY="$BREW/opt/openssl@3/lib/libcrypto.dylib" \
    -DOPENSSL_SSL_LIBRARY="$BREW/opt/openssl@3/lib/libssl.dylib" \
    -DCMAKE_PREFIX_PATH="$BREW/opt/openssl@3;$BREW/opt/qt@6;$BREW/opt/protobuf@29"

echo "==> Building chiaki-ng"
export CPATH="$BREW/opt/ffmpeg/include"
cmake --build build --config Release --clean-first --target chiaki

echo "==> Deploying into chiaki-ng.app"
rm -rf chiaki-ng.app chiaki-ng.dmg
cp -a build/gui/chiaki.app chiaki-ng.app
cp scripts/qtwebengine_import.qml gui/src/qml/

"$BREW/opt/qt@6/bin/macdeployqt" chiaki-ng.app \
    -qmldir="$PWD/gui/src/qml" \
    -libpath="$BREW/lib"

echo "==> Bundling MoltenVK"
mkdir -p chiaki-ng.app/Contents/Resources/vulkan/icd.d
wget -q https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar
tar xf MoltenVK-macos.tar
cp MoltenVK/MoltenVK/dynamic/dylib/macOS/* chiaki-ng.app/Contents/Resources/vulkan/icd.d
rm -rf MoltenVK MoltenVK-macos.tar

# Second macdeployqt pass picks up any libraries MoltenVK might have added
"$BREW/opt/qt@6/bin/macdeployqt" chiaki-ng.app \
    -qmldir="$PWD/gui/src/qml" \
    -libpath="$BREW/lib"

if [[ -d chiaki-ng.app/Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app ]]; then
    ln -sf ../../../../../../../Frameworks \
        chiaki-ng.app/Contents/Frameworks/QtWebEngineCore.framework/Helpers/QtWebEngineProcess.app/Contents
fi

echo "==> Fixing SDL library paths"
cp /usr/local/lib/libSDL3.dylib         chiaki-ng.app/Contents/MacOS/libSDL3.dylib
cp /usr/local/lib/libSDL2-2.0.0.dylib   chiaki-ng.app/Contents/MacOS/libSDL2-2.0.0.dylib

install_name_tool -id @executable_path/libSDL3.dylib       chiaki-ng.app/Contents/MacOS/libSDL3.dylib
install_name_tool -id @executable_path/libSDL2-2.0.0.dylib chiaki-ng.app/Contents/MacOS/libSDL2-2.0.0.dylib

for path in /usr/local/lib/libSDL3.dylib @rpath/libSDL3.dylib "$BREW/lib/libSDL3.dylib"; do
    install_name_tool -change "$path" @executable_path/libSDL3.dylib \
        chiaki-ng.app/Contents/MacOS/libSDL2-2.0.0.dylib 2>/dev/null || true
done
for path in /usr/local/lib/libSDL2-2.0.0.dylib @rpath/libSDL2-2.0.0.dylib "$BREW/lib/libSDL2-2.0.0.dylib"; do
    install_name_tool -change "$path" @executable_path/libSDL2-2.0.0.dylib \
        chiaki-ng.app/Contents/MacOS/chiaki 2>/dev/null || true
done

ln -sf libvulkan.1.dylib chiaki-ng.app/Contents/Frameworks/vulkan

echo "==> Ad-hoc signing"
codesign --force --entitlements gui/entitlements.xml --deep --sign - chiaki-ng.app

echo "==> Creating chiaki-ng.dmg"
bash scripts/macos-dmg.sh chiaki-ng.app chiaki-ng.dmg

echo ""
echo "Done! Open chiaki-ng.dmg and drag the app to Applications."
