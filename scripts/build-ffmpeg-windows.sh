#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=n9.0.2
NV_CODEC_TAG=n13.1.15.0

if [ ! -d "nv-codec-headers" ]; then
    git clone https://github.com/FFmpeg/nv-codec-headers.git || exit 1
fi
cd nv-codec-headers || exit 1
git checkout $NV_CODEC_TAG || exit 1
make PREFIX=/mingw64 install || exit 1
cd .. || exit 1

git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg --depth 1 -b $TAG && cd ffmpeg || exit 1
git apply ${SCRIPT_DIR}/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch || exit 1
./configure --disable-all --enable-shared --enable-ffmpeg --enable-avformat --enable-avcodec --enable-decoder=h264 --enable-decoder=hevc --enable-decoder=h264_cuvid --enable-decoder=hevc_cuvid --enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan --enable-hwaccel=h264_d3d11va --enable-hwaccel=hevc_d3d11va --enable-hwaccel=h264_nvdec --enable-hwaccel=hevc_nvdec --enable-cuvid --enable-nvdec --enable-ffnvcodec --prefix=/mingw64 "$@" || exit 1
make -j4 || exit 1
make install || exit 1
