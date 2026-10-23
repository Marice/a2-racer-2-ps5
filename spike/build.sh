#!/usr/bin/env bash
# A2 Racer II for PS5 - builds the Phase 0 check (spike/gl-compat) into a title folder, PPSA69427.
#
# Uses stk-ps5's PS5 support files (copied to ../ps5) and, for now, the PacBrew sysroot, the OpenGL SDK 1.0.2 and
# ps5-native-tool from the sibling projects; phase 1 fetches its own (plan, phase 1).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/.." && pwd)
workspace=$(cd -- "$root/.." && pwd)
: "${PS5_PAYLOAD_SDK:=/opt/ps5-payload-sdk}"
: "${PACBREW:=$workspace/olise-player/.deps/pacbrew/v0.40.2/sysroot/user/homebrew}"
: "${PS5_OPENGL_SDK:=$workspace/stk-ps5/sdk-102/unpacked/ps5-opengl-sdk-1.0.2/sdk}"
: "${PS5_NATIVE_TOOL:=$workspace/olise-player/build/host/ps5-native-tool}"
export PS5_PAYLOAD_SDK PACBREW

for need in "$PACBREW/lib" "$PS5_OPENGL_SDK/lib"; do
    [ -d "$need" ] || { echo "spike/build.sh: $need not found" >&2; exit 1; }
done
[ -x "$PS5_NATIVE_TOOL" ] || { echo "spike/build.sh: ps5-native-tool not found at $PS5_NATIVE_TOOL" >&2; exit 1; }

build="$root/build-spike"
cmake -B "$build" -S "$here/gl-compat" \
    -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
    -DCMAKE_PROJECT_INCLUDE_BEFORE="$root/ps5/ps5-gl.cmake" \
    -DPKG_CONFIG_EXECUTABLE="$root/ps5/ps5-pkg-config" \
    -DPS5_OPENGL_SDK="$PS5_OPENGL_SDK" \
    -DCMAKE_PREFIX_PATH="$PACBREW" -DCMAKE_FIND_ROOT_PATH="$PACBREW" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j"$(nproc)"

title="$build/pkg/PPSA69427"
mkdir -p "$title/sce_sys" "$build/pkg/stubs"
# The console's module stubs the converter links against (from stk-ps5's title build for now).
cp "$workspace/stk-ps5/pkg/stubs/"*.so "$build/pkg/stubs/"
"$PS5_NATIVE_TOOL" link --in "$build/a2_gl_compat" --out "$build/pkg/eboot.elf" --stub-dir "$build/pkg/stubs" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
"$PS5_NATIVE_TOOL" self --sign --in "$build/pkg/eboot.elf" --out "$title/eboot.bin" --magic 0x1D3D154F
# The C library the title imports; without it the console answers PRX_SCE_MODULE_LOAD_ERROR (0xa0020102).
mkdir -p "$title/sce_module"
cp "$workspace/stk-ps5/pkg/PPSA69421/sce_module/libc.prx" "$title/sce_module/libc.prx"
cp "$here/param.json" "$title/sce_sys/param.json"
cp "$workspace/stk-ps5/sce_sys/icon0.png" "$title/sce_sys/icon0.png" # placeholder icon for the check only
ls -la "$title" "$title/sce_sys"
