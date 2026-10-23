#!/usr/bin/env bash
# Puts the PS5 OpenGL SDK 1.0.2 in .deps/ps5-opengl/1.0.2 and prints its sdk folder (PS5_OPENGL_SDK).
#
# The original repository (blackbearreloaded/ps5-opengl) is gone; darkxex/ps5-opengl mirrors its 1.0.2 release.
# The archive is checked against the hash stk-ps5 used (decision log D3). A copy that is already on this machine
# (stk-ps5's) is used instead of downloading 226 MB. Does nothing when the SDK is already extracted.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=1.0.2
hash=30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4
url="https://github.com/darkxex/ps5-opengl/releases/download/v$version/ps5-opengl-sdk-$version.tar.gz"
local_copy="$root/../stk-ps5/sdk-102/ps5-opengl-sdk-$version.tar.gz"
cache="$root/.deps/ps5-opengl"
release="$cache/$version"
archive="$cache/ps5-opengl-sdk-$version.tar.gz"

check() { printf '%s  %s\n' "$hash" "$1" | sha256sum --check --strict >/dev/null 2>&1; }

if [[ ! -f $release/.complete ]]; then
    mkdir -p "$cache"
    if [[ ! -f $archive ]] || ! check "$archive"; then
        if [[ -f $local_copy ]] && check "$local_copy"; then
            cp "$local_copy" "$archive"
        else
            echo "setup-glsdk.sh: downloading the PS5 OpenGL SDK $version (about 226 MB)" >&2
            curl -sfL "$url" -o "$archive.download"
            check "$archive.download" || { echo "setup-glsdk.sh: checksum mismatch" >&2; exit 2; }
            mv "$archive.download" "$archive"
        fi
    fi
    rm -rf "$release"
    mkdir -p "$release"
    tar -xzf "$archive" -C "$release" --strip-components=1
    [[ -f $release/sdk/lib/libPS5OpenGL.a ]] || { echo "setup-glsdk.sh: unexpected archive layout" >&2; exit 2; }
    touch "$release/.complete"
fi
echo "$release/sdk"
