#!/usr/bin/env bash
# Zips the title folder build.sh made (pkg/PPSA69428) for a release, with its sha256.
# The folder never holds game data: players put their own disc image in dump/ (README).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
title_id=PPSA69428
app="$root/pkg/$title_id"
for f in eboot.bin sce_module/libc.prx sce_sys/param.json sce_sys/icon0.png sce_sys/pic0.dds sce_sys/pic1.dds; do
    [[ -f $app/$f ]] || { echo "package.sh: $app/$f is missing; run build.sh (and tools/make-backgrounds.py)" >&2; exit 1; }
done
if [[ -e $app/data ]] || find "$app/dump" -type f ! -name 'PUT-THE-DISC-IMAGE-HERE.txt' | grep -q .; then
    echo "package.sh: $app holds game data; a release must not" >&2
    exit 1
fi
version=$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["contentVersion"])' "$app/sce_sys/param.json")
mkdir -p "$root/dist"
zip="$root/dist/A2Racer2-PS5-$version.zip"
rm -f "$zip"
(cd "$root/pkg" && zip -qr -9 "$zip" "$title_id" -x "$title_id/stubs/*")
(cd "$root/dist" && sha256sum "$(basename "$zip")" > "$(basename "$zip").sha256")
echo "package.sh: $zip ($(du -h "$zip" | cut -f1))"
