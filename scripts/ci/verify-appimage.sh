#!/usr/bin/env bash
set -euo pipefail
image=$(realpath "${1:?AppImage path}")
expected=${2:?Expected version}
repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$repo/build/ci/linux-smoke"
smoke=$(mktemp -d)
trap 'rm -rf "$smoke"' EXIT
cd "$smoke"
"$image" --appimage-extract > "$repo/build/ci/appimage-extract.log"
root=$smoke/squashfs-root
test -x "$root/AppRun"
test -x "$root/usr/bin/chiaki"
test -s "$root/usr/share/applications/chiaking.desktop"
find "$root" -name '*chiaking*.png' | grep -q .
for name in 'libSDL2*.so*' 'libSDL3*.so*' 'libplacebo*.so*'; do
    find "$root" -name "$name" | grep -q .
done
mkdir -p home config cache data
# The runner has no Qt installation. Do not inherit the build container's paths.
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH QT_QPA_PLATFORM_PLUGIN_PATH
export HOME="$smoke/home" XDG_CONFIG_HOME="$smoke/config" XDG_CACHE_HOME="$smoke/cache" XDG_DATA_HOME="$smoke/data"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software SDL_AUDIODRIVER=dummy QTWEBENGINE_DISABLE_SANDBOX=1
export CHIAKI_DIAGNOSTIC_LOG_DIR="$repo/build/ci/linux-smoke"
version=$(timeout 60 "$root/AppRun" --version)
[[ "$version" == *"$expected"* ]] || { echo '[App][Package] Runtime version mismatch' >&2; exit 1; }
timeout 60 "$root/AppRun" --package-smoke-test
echo '[App][Package] AppImage native startup, Qt/QML, SDL and launcher resources passed; no GPU/console test claimed.'
