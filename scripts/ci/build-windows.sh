#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/ci deps/ffmpeg
pacman -U --noconfirm --needed deps/msys/*.pkg.tar.zst
pacman -Q > build/ci/windows-packages.txt
export CHIAKI_RELEASE_LOCKED=1 INSTALL_PREFIX=/clang64
export LIBPLACEBO_VERSION=3be579e6c7f6b421d9ac3ab4860edc437c50b3eb
python scripts/ci/release.py download --group ffmpeg-windows --folder deps/ffmpeg.zip
unzip -q deps/ffmpeg.zip -d deps/ffmpeg
for directory in deps/ffmpeg/*; do
    cp -a "$directory/bin/." /clang64/bin/
    cp -a "$directory/include/." /clang64/include/
    cp -a "$directory/lib/." /clang64/lib/
done
scripts/build-sdl2-compat.sh .
cp /clang64/lib/pkgconfig/sdl2-compat.pc /clang64/lib/pkgconfig/sdl2.pc
scripts/build-libplacebo-windows.sh .
cmake -S . -B build/ci-gui -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCHIAKI_ENABLE_CLI=OFF \
    -DCHIAKI_ENABLE_TESTS=ON -DCHIAKI_ENABLE_FFMPEG_DECODER=ON \
    -DCHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER=ON -DCHIAKI_ENABLE_STEAMDECK_NATIVE=OFF
cmake --build build/ci-gui --parallel 2
ctest --test-dir build/ci-gui --output-on-failure
cmake -S gui/enhancement/tests -B build/ci-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ci-tests --parallel 2
ctest --test-dir build/ci-tests --output-on-failure
# Reuse the existing deployment scanner and Qt QML deployment tool.
mkdir -p build/ci-stage
cp /clang64/bin/vulkan-1.dll build/ci-stage/
scripts/deploy-windows-msys2.sh build/ci-stage build/ci-gui/gui/chiaki.exe \
    "$PWD/build/ci-gui/third-party/cpp-steam-tools" /clang64 gui/src/qml
