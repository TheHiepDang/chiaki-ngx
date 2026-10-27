#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
export CHIAKI_RELEASE_LOCKED=1 APPIMAGE_EXTRACT_AND_RUN=1
export FFMPEG_COMMIT=b08d7969c550a804a59511c7b83f2dd8cc0499b8
export NV_CODEC_TAG=e844e5b26f46bb77479f063029595293aa8f812d
export HIDAPI_VERSION=d6b2a974608dec3b76fb1e36c189f22b9cf3650c
cp scripts/qtwebengine_import.qml gui/src/qml/
scripts/build-appimage.sh /build/appdir
qtroot="$(find "$QT_PATH" -maxdepth 1 -type d -name "$QT_VERSION")/gcc_64"
export PATH="$qtroot/bin:$PATH"
cmake -S gui/enhancement/tests -B build/ci-tests -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qtroot"
cmake --build build/ci-tests --parallel 2
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ctest --test-dir build/ci-tests --output-on-failure
