#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
app_path="$project_dir/build-es/bin/chiaki"
if [[ ! -x "$app_path" ]]; then
    printf '%s\n' 'Falta compilar esta versión. Consulta README_ES.md.' >&2
    exit 1
fi

export CHIAKI_LANG="${CHIAKI_LANG:-es}"
export LD_LIBRARY_PATH="$project_dir/build-es/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$app_path" "$@"
