#!/bin/bash

export SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
shift
ROOT="`pwd`"

NV_CODEC_TAG=${NV_CODEC_TAG:-e844e5b26f46bb77479f063029595293aa8f812d}

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

# Use the same immutable source on PRs, manual builds and production releases.
FFMPEG_COMMIT=${FFMPEG_COMMIT:-b08d7969c550a804a59511c7b83f2dd8cc0499b8}
[[ "$FFMPEG_COMMIT" =~ ^[0-9a-f]{40}$ ]] || { echo 'FFmpeg requires a full commit SHA' >&2; exit 1; }
git init ffmpeg && git -C ffmpeg remote add origin https://github.com/FFmpeg/FFmpeg.git || exit 1
git -C ffmpeg fetch --depth 1 origin "$FFMPEG_COMMIT" || exit 1
git -C ffmpeg checkout --detach FETCH_HEAD || exit 1
cd ffmpeg || exit 1

git apply ${SCRIPT_DIR}/flatpak/0001-vulkan-ignore-frames-without-hw-context.patch || exit 1
./configure --disable-all --enable-static --enable-avformat --enable-vulkan --enable-libdrm --enable-avcodec --enable-decoder=h264 --enable-decoder=hevc --enable-hwaccel=h264_vaapi --enable-hwaccel=hevc_vaapi --enable-hwaccel=h264_vulkan --enable-hwaccel=hevc_vulkan "${FFMPEG_NV_CODEC_FLAGS[@]}" --prefix=/usr "$@" || exit 1
make -j4 || exit 1
make install || exit 1
