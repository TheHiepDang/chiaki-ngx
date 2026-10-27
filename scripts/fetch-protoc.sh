#!/bin/bash

set -xe
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

cd $(dirname "${BASH_SOURCE[0]}")/..
cd "./$1"
ROOT="`pwd`"

if [ "$(uname -m)" = "aarch64" ]
then
    export ARCH_SUFFIX="aarch_64"
else
    export ARCH_SUFFIX="x86_64"
fi

URL=https://github.com/protocolbuffers/protobuf/releases/download/v3.9.1/protoc-3.9.1-linux-${ARCH_SUFFIX}.zip

if [[ ${CHIAKI_RELEASE_LOCKED:-0} == 1 ]]; then
    python3 "$SCRIPT_DIR/ci/release.py" download --group protoc --folder protoc.zip
else
    curl -fL "$URL" -o protoc.zip
fi
unzip protoc.zip -d protoc

