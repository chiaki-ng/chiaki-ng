#!/bin/bash

set -xe

if [ "$(uname -m)" = "aarch64" ]
then
    export GCC_STRING="gcc_arm64"
else
    export GCC_STRING="gcc_64"
fi

export QT_DIR="$(find ${QT_PATH} -maxdepth 1 -type d -name "${QT_VERSION}")"
export PATH="${QT_DIR}/${GCC_STRING}/bin:$PATH"
if [ -f "${HOME}/chiaki-venv/bin/activate" ]
then
   source "${HOME}/chiaki-venv/bin/activate"
fi

# sometimes there are errors in linuxdeploy in docker/podman when the appdir is on a mount
appdir=${1:-`pwd`/appimage/appdir}

rm -rf appimage && mkdir -p appimage

scripts/fetch-protoc.sh appimage
export PATH="`pwd`/appimage/protoc/bin:$PATH"
scripts/build-ffmpeg.sh appimage
scripts/build-sdl2-compat.sh appimage
scripts/build-libplacebo.sh appimage
scripts/build-hidapi.sh appimage

rm -rf build_appimage && mkdir -p build_appimage
cd build_appimage 
qt-cmake \
	-GNinja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_GUI=ON \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	-DCMAKE_INSTALL_PREFIX=/usr \
	..
cd ..

# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

ninja -C build_appimage
build_appimage/test/chiaki-unit

DESTDIR="${appdir}" ninja -C build_appimage install

# linuxdeploy does not reliably pick up libSDL3 when it is only required through
# the packaged sdl2-compat shim. Stage both SDL runtimes explicitly so the
# resulting AppImage can load the controller backend without host SDL3.
mkdir -p "${appdir}/usr/lib"
shopt -s nullglob
SDL_LIB_DIRS=(
    "$(pwd)"/appimage/SDL3-*/build
    "$(pwd)"/appimage/sdl2-compat-*/build
)
shopt -u nullglob
if [ "${#SDL_LIB_DIRS[@]}" -eq 0 ]; then
    echo "Failed to locate SDL build directories for AppImage staging" >&2
    exit 1
fi
while IFS= read -r -d '' sdl_lib; do
    cp -a "${sdl_lib}" "${appdir}/usr/lib/"
done < <(find "${SDL_LIB_DIRS[@]}" \
    -type f,l \
    \( -name 'libSDL2*.so*' -o -name 'libSDL3*.so*' \) \
    -print0 2>/dev/null)

cd appimage

export ARCH="$(uname -m)"
curl -L -O https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage
chmod +x linuxdeploy-${ARCH}.AppImage

export LD_LIBRARY_PATH="${QT_DIR}/${GCC_STRING}/lib:$(pwd)/../build_appimage/third-party/cpp-steam-tools:$LD_LIBRARY_PATH"
export QML_SOURCES_PATHS="$(pwd)/../gui/src/qml"
export EXTRA_QT_MODULES="waylandclient;waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so;libqeglfs.so;libqminimal.so;libqminimalegl.so;libqvkkhrdisplay.so;libqvnc.so;libqoffscreen.so;libqlinuxfb.so"
curl -L -O https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage
chmod +x linuxdeploy-plugin-qt-${ARCH}.AppImage

# Qt WebEngine's bundled NSS loads its crypto modules at runtime. Without them the
# host's modules are loaded instead, NSS fails to initialize and the PSN login crashes.
NSS_LIB_DIR="/usr/lib/$(gcc -print-multiarch)"
NSS_MODULE_ARGS=()
for nss_module in libsoftokn3.so libfreebl3.so libfreeblpriv3.so libnssckbi.so; do
    # Ubuntu 20.04 keeps the modules in an nss subdirectory
    if [ -f "${NSS_LIB_DIR}/nss/${nss_module}" ]; then
        NSS_MODULE_ARGS+=(--library="${NSS_LIB_DIR}/nss/${nss_module}")
    else
        NSS_MODULE_ARGS+=(--library="${NSS_LIB_DIR}/${nss_module}")
    fi
done

./linuxdeploy-${ARCH}.AppImage \
    --appdir="${appdir}" \
    -e "${appdir}/usr/bin/chiaki" \
    -d "${appdir}/usr/share/applications/chiaking.desktop" \
    "${NSS_MODULE_ARGS[@]}" \
    --plugin qt \
    --exclude-library='libva*' \
    --exclude-library='libvulkan*' \
    --output appimage

# sdl2-compat must find the bundled SDL3 before any copy on the host
for sdl2_lib in "${appdir}"/usr/lib/libSDL2-2.0.so.0*; do
    sdl2_rpath="$(readelf -d "${sdl2_lib}" | grep -E 'R(UN)?PATH' || true)"
    if [[ "${sdl2_rpath}" != *'$ORIGIN'* || "${sdl2_rpath}" == *'/usr/'* ]]; then
        echo "${sdl2_lib} doesn't search its own directory first for SDL3: ${sdl2_rpath}" >&2
        exit 1
    fi
done

# NSS must load its modules from the AppImage, not from the host
for nss_module in libsoftokn3.so libfreebl3.so libfreeblpriv3.so libnssckbi.so; do
    if [ ! -f "${appdir}/usr/lib/${nss_module}" ]; then
        echo "${nss_module} isn't bundled next to libnss3.so" >&2
        exit 1
    fi
done
mv chiaki-ng-${ARCH}.AppImage chiaki-ng.AppImage
