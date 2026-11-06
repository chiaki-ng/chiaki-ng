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

rm -rf deb && mkdir -p deb

scripts/fetch-protoc.sh deb
export PATH="`pwd`/deb/protoc/bin:$PATH"
scripts/build-ffmpeg.sh deb
scripts/build-sdl2-compat.sh deb
scripts/build-libplacebo.sh deb
scripts/build-hidapi.sh deb

rm -rf build_deb && mkdir -p build_deb
cd build_deb
qt-cmake \
	-GNinja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCHIAKI_ENABLE_TESTS=ON \
	-DCHIAKI_ENABLE_GUI=ON \
	-DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON \
	-DCMAKE_INSTALL_PREFIX=/usr \
	-DCPACK_PACKAGE_CONTACT="chiaki-ng contributors <https://github.com/streetpea/chiaki-ng>" \
	..
cd ..

# purge leftover proto/nanopb_pb2.py which may have been created with another protobuf version
rm -fv third-party/nanopb/generator/proto/nanopb_pb2.py

ninja -C build_deb
build_deb/test/chiaki-unit

# install into a staging dir and produce the .deb via CPack
DESTDIR="$(pwd)/deb/pkg" ninja -C build_deb install
cd build_deb
cpack -G DEB
mv chiaki-ng-*.deb ../deb/
cd ..
