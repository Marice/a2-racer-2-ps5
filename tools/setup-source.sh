#!/usr/bin/env bash
# Checks out A2 Racer II's reimplementation (berobloom/a2racer2-re) at the pinned commit into src-upstream/ and
# applies patches/a2racer2.patch. patches/ is the source of truth: edit inside src-upstream/, then save the change
# with tools/save-patch.sh. Refuses to touch a src-upstream/ with changes that are not saved in the patch yet.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
repo=https://github.com/berobloom/a2racer2-re
commit=712f7e8
src="$root/src-upstream"
patch="$root/patches/a2racer2.patch"

if [[ ! -d $src/.git ]]; then
    git clone --quiet "$repo" "$src"
fi
if [[ -n $(git -C "$src" status --porcelain) ]]; then
    saved=$(mktemp)
    git -C "$src" diff > "$saved"
    if ! cmp -s "$saved" "$patch"; then
        rm -f "$saved"
        echo "setup-source.sh: src-upstream/ has changes not in patches/a2racer2.patch; run tools/save-patch.sh first" >&2
        exit 1
    fi
    rm -f "$saved"
    git -C "$src" checkout --quiet -- .
fi
git -C "$src" checkout --quiet "$commit"
if [[ -s $patch ]]; then
    git -C "$src" apply --whitespace=nowarn "$patch"
fi
echo "$src at $commit$( [[ -s $patch ]] && echo ", patched")"
