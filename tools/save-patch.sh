#!/usr/bin/env bash
# Writes the changes made inside src-upstream/ to patches/a2racer2.patch (see tools/setup-source.sh).
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
git -C "$root/src-upstream" add -N .
git -C "$root/src-upstream" diff > "$root/patches/a2racer2.patch"
echo "patches/a2racer2.patch: $(grep -c '^diff --git' "$root/patches/a2racer2.patch") file(s)"
