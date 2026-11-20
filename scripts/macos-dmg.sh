#!/usr/bin/env bash
# Packages chiaki-ng.app as a DMG with an Applications shortcut to drag it onto.
# usage: macos-dmg.sh <chiaki-ng.app> <output.dmg> [label]
# A label, such as "weekly", is drawn on the background as "WEEKLY BUILD".
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "usage: $0 <chiaki-ng.app> <output.dmg> [label]" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "$0")" && pwd)"
app="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
dmg="$2"
label="${3:-}"

work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

swift "$script_dir/macos/dmg-background.swift" "$work_dir" ${label:+"$label"}
tiffutil -cathidpicheck "$work_dir/dmg-background.png" "$work_dir/dmg-background@2x.png" \
    -out "$work_dir/dmg-background.tiff"

# DMGBUILD can point at an installed dmgbuild, as CI does so nothing is downloaded while signing;
# otherwise install the pinned wheels into a throwaway environment
dmgbuild="${DMGBUILD:-}"
if [[ -z "$dmgbuild" ]]; then
    python3 -m venv "$work_dir/venv"
    "$work_dir/venv/bin/pip" install --quiet --disable-pip-version-check --require-hashes \
        --only-binary :all: -r "$script_dir/macos/dmgbuild-requirements.txt"
    dmgbuild="$work_dir/venv/bin/dmgbuild"
fi

rm -f "$dmg"
"$dmgbuild" -s "$script_dir/macos/dmg-settings.py" \
    -D app="$app" -D background="$work_dir/dmg-background.tiff" \
    chiaki-ng "$dmg"
