#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

TAG=n9.0.2
NV_CODEC_TAG=n13.1.15.0

FFMPEG_NV_CODEC_FLAGS=()
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ]
then
    if [ ! -d "nv-codec-headers" ]; then
        git clone https://github.com/FFmpeg/nv-codec-headers.git || exit 1
    fi
    cd nv-codec-headers || exit 1
    git checkout $NV_CODEC_TAG || exit 1
    make PREFIX=/usr install || exit 1
    cd .. || exit 1

    FFMPEG_NV_CODEC_FLAGS=(
        --enable-decoder=h264_cuvid
        --enable-decoder=hevc_cuvid
        --enable-hwaccel=h264_nvdec
        --enable-hwaccel=hevc_nvdec
        --enable-cuvid
        --enable-nvdec
        --enable-ffnvcodec
    )
fi

git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg --depth 1 -b $TAG && cd ffmpeg || exit 1
git apply ${SCRIPT_DIR}/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch || exit 1
./configure --disable-all --enable-static --enable-avformat --enable-vulkan --enable-libdrm --enable-avcodec --enable-decoder=h264 --enable-decoder=hevc --enable-hwaccel=h264_vaapi --enable-hwaccel=hevc_vaapi --enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan "${FFMPEG_NV_CODEC_FLAGS[@]}" --prefix=/usr "$@" || exit 1
make -j4 || exit 1
make install || exit 1
