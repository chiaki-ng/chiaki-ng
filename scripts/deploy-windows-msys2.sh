#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 <output-dir> <exe-path> <tool-dir> <msys-prefix> <qml-dir>" >&2
    exit 1
fi

output_dir="$1"
exe_path="$2"
tool_dir="$3"
msys_prefix="$4"
qml_dir="$5"

mkdir -p "$output_dir"
cp "$exe_path" "$output_dir/"

export PATH="${tool_dir}:${msys_prefix}/share/qt6/bin:${PATH}"
export QT_PLUGIN_PATH="${msys_prefix}/share/qt6/plugins"
export QML2_IMPORT_PATH="${msys_prefix}/share/qt6/qml"

# Read each binary's import table with objdump instead of running ldd: ldd loads the binary, and
# under the x64 emulation MSYS2 uses on Windows ARM that load could hang the job for hours
objdump_cmd=""
for candidate in llvm-objdump objdump; do
    if command -v "$candidate" >/dev/null; then
        objdump_cmd="$candidate"
        break
    fi
done
if [[ -z "$objdump_cmd" ]]; then
    echo "error: neither llvm-objdump nor objdump found" >&2
    exit 1
fi

# Where the imports are looked up; anything not found here is a Windows system DLL
search_dirs=("$tool_dir" "$msys_prefix/bin")

# DLLs to leave for Windows to supply. The GPU driver installs its own Vulkan loader, so use that
# rather than a bundled one that may not work with the installed driver
skip_dlls=("vulkan-1.dll")

declare -A queued_paths=()
declare -A scanned_paths=()

queue=("$output_dir/$(basename "$exe_path")")
queued_paths["$queue"]=1

extract_dependencies() {
    local binary="$1"
    local imports
    if ! imports="$("$objdump_cmd" -p "$binary" | awk '/DLL Name:/ { sub(/\r$/, "", $3); print $3 }')"; then
        echo "error: $objdump_cmd could not read $binary" >&2
        exit 1
    fi

    local name dir
    while IFS= read -r name; do
        [[ -n "$name" ]] || continue
        local skip skipped=false
        for skip in "${skip_dlls[@]}"; do
            if [[ "${name,,}" == "${skip,,}" ]]; then
                skipped=true
                break
            fi
        done
        [[ "$skipped" == false ]] || continue
        for dir in "${search_dirs[@]}"; do
            if [[ -f "$dir/$name" ]]; then
                printf '%s\n' "$dir/$name"
                break
            fi
        done
    done <<< "$imports"
}

enqueue_dependency() {
    local dependency="$1"
    local file_name

    [[ -n "$dependency" ]] || return 0

    file_name="${dependency##*/}"
    if [[ ! -e "$output_dir/$file_name" ]]; then
        echo "Copied $dependency"
        cp "$dependency" "$output_dir/"
    fi

    if [[ -z "${queued_paths["$dependency"]+x}" ]]; then
        queue+=("$dependency")
        queued_paths["$dependency"]=1
    fi
}

while [[ ${#queue[@]} -gt 0 ]]; do
    current="${queue[0]}"
    queue=("${queue[@]:1}")

    if [[ -n "${scanned_paths["$current"]+x}" ]]; then
        continue
    fi
    scanned_paths["$current"]=1

    # A command substitution, not a process substitution, so a failed read stops the script
    dependencies="$(extract_dependencies "$current")"
    while IFS= read -r dependency; do
        enqueue_dependency "$dependency"
    done <<< "$dependencies"
done

# Ensure SDL3 is bundled even if ldd/Qt doesn't list it.
shopt -s nullglob
for dll_dir in "$msys_prefix/bin" "/clangarm64/bin" "$msys_prefix/mingw64/bin"; do
    for sdl3 in "$dll_dir"/SDL3*.dll; do
        if [[ -f "$sdl3" ]]; then
            echo "Staging SDL3 runtime $sdl3"
            cp "$sdl3" "$output_dir/"
        fi
    done
done
shopt -u nullglob

windeployqt6.exe --no-translations --qmldir="$qml_dir" "$output_dir/$(basename "$exe_path")"

# Remove system-provided DLLs that Windows already supplies and
# should not be bundled with the MSYS2 build (e.g. D3D compiler). Use
# case-insensitive globbing to catch variants like `D3Dcompiler_47`.
(
    shopt -s nocaseglob
    rm -f "$output_dir"/d3dcompiler*.dll
)
