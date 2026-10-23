#!/usr/bin/env bash
# Builds A2 Racer II for the PS5 into pkg/<title>/ (decision log D1, D3, D4).
#
#   tools/setup-source.sh   once: upstream at the pinned commit, with patches/a2racer2.patch
#   ./build.sh              PacBrew and the OpenGL SDK come from tools/setup-*.sh (checked by hash)
#
# ps5-native-tool, the console's module stubs and sce_module/libc.prx still come from the sibling projects
# (olise-player, stk-ps5): the same ones stk-ps5's title runs with.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace=$(cd -- "$root/.." && pwd)
title_id=PPSA69428
: "${PS5_PAYLOAD_SDK:=/opt/ps5-payload-sdk}"
: "${PS5_NATIVE_TOOL:=$workspace/olise-player/build/host/ps5-native-tool}"
: "${PS5_STUBS:=$workspace/stk-ps5/pkg/stubs}"
: "${PS5_LIBC_PRX:=$workspace/stk-ps5/pkg/PPSA69421/sce_module/libc.prx}"
PACBREW=$("$root/tools/setup-pacbrew.sh")
PS5_OPENGL_SDK=$("$root/tools/setup-glsdk.sh")
export PS5_PAYLOAD_SDK PACBREW

[ -d "$root/src-upstream/port" ] || { echo "build.sh: run tools/setup-source.sh first" >&2; exit 1; }
for need in "$PS5_NATIVE_TOOL" "$PS5_LIBC_PRX"; do
    [ -e "$need" ] || { echo "build.sh: $need not found" >&2; exit 1; }
done

build="$root/build-ps5"
cmake -B "$build" -S "$root" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_PROJECT_INCLUDE_BEFORE="$root/ps5/ps5-gl.cmake" \
    -DPKG_CONFIG_EXECUTABLE="$root/ps5/ps5-pkg-config" \
    -DPS5_OPENGL_SDK="$PS5_OPENGL_SDK" \
    -DCMAKE_PREFIX_PATH="$PACBREW" -DCMAKE_FIND_ROOT_PATH="$PACBREW" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"

# The ELF becomes a title binary: converted against the console's module stubs, then signed as a fake SELF.
pkg="$root/pkg"
title="$pkg/$title_id"
mkdir -p "$title/sce_sys" "$title/sce_module" "$title/dump" "$pkg/stubs"
cp "$PS5_STUBS/"*.so "$pkg/stubs/"
"$PS5_NATIVE_TOOL" link --in "$build/a2racer2" --out "$pkg/eboot.elf" --stub-dir "$pkg/stubs" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$PS5_NATIVE_TOOL" self --sign --in "$pkg/eboot.elf" --out "$title/eboot.bin" --magic 0x1D3D154F
# The C library the title imports; without it: PRX_SCE_MODULE_LOAD_ERROR (D2).
cp "$PS5_LIBC_PRX" "$title/sce_module/libc.prx"
cp "$root/sce_sys/param.json" "$root/sce_sys/icon0.png" "$title/sce_sys/"
for pic in pic0.dds pic1.dds; do
    [ -f "$root/sce_sys/$pic" ] && cp "$root/sce_sys/$pic" "$title/sce_sys/"
done
# The player's disc image goes here (README); an empty folder shows where.
printf 'Put the A2 Racer II disc image (.bin and .cue) in this folder.\n' > "$title/dump/PUT-THE-DISC-IMAGE-HERE.txt"
ls -la "$title"
