#!/bin/bash

set -xe

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

SDL_VER=2.32.10
URL=https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz
FILE=SDL2-${SDL_VER}.tar.gz
DIR=SDL2-${SDL_VER}

if [ ! -d "$DIR" ]; then
	curl -L "$URL" -O
	tar -xf "$FILE"
fi

cd "$DIR" || exit 1

INSTALL_PREFIX="${INSTALL_PREFIX:=/usr}"

cmake \
	-G "Ninja" -B build -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
	-DSDL_ATOMIC=ON \
	-DSDL_AUDIO=ON \
	-DSDL_CPUINFO=ON \
	-DSDL_EVENTS=ON \
	-DSDL_FILE=OFF \
	-DSDL_FILESYSTEM=OFF \
	-DSDL_HAPTIC=ON \
	-DSDL_JOYSTICK=ON \
	-DSDL_LOADSO=ON \
	-DSDL_RENDER=OFF \
	-DSDL_SHARED=ON \
	-DSDL_STATIC=OFF \
	-DSDL_TEST=OFF \
	-DSDL_THREADS=ON \
	-DSDL_TIMERS=ON \
	-DSDL_VIDEO=OFF \
	-DSDL_OSS=OFF \
	-DSDL_ALSA=OFF \
	-DSDL_JACK=OFF \
	-DSDL_ESD=OFF \
	-DSDL_NAS=OFF \
	-DSDL_SNDIO=OFF \
	-DSDL_ARTS=OFF \
	-DSDL_PULSEAUDIO=OFF \
	-DSDL_PIPEWIRE=ON
# SDL_THREADS is not needed, but it doesn't compile without

cmake --build build --config RelWithDebInfo --clean-first
cmake --install build --prefix "$INSTALL_PREFIX"
