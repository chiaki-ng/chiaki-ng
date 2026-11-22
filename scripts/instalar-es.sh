#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ -x "$project_dir/build-es/bin/chiaki" ]] || { echo 'Falta compilar la aplicación.' >&2; exit 1; }
applications_dir="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
desktop_path="$applications_dir/io.github.streetpea.Chiaki4deck.desktop"
backup_dir="${XDG_STATE_HOME:-$HOME/.local/state}/chiaki-ng-es/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$applications_dir" "$backup_dir"
if [[ -e "$desktop_path" ]]; then cp -p "$desktop_path" "$backup_dir/"; fi
if [[ -d "${XDG_CONFIG_HOME:-$HOME/.config}/Chiaki" ]]; then
    cp -a "${XDG_CONFIG_HOME:-$HOME/.config}/Chiaki" "$backup_dir/"
fi
cat > "$desktop_path" <<EOF
[Desktop Entry]
Type=Application
Name=chiaki-ng
Comment=Uso a distancia de PlayStation · Español y temas de juegos
Exec="$project_dir/scripts/iniciar-es.sh"
Icon=$project_dir/gui/res/chiaking.svg
Categories=Game;Network;RemoteAccess;
StartupWMClass=chiaki-ng
Terminal=false
EOF
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$desktop_path"; fi
if command -v update-desktop-database >/dev/null; then update-desktop-database "$applications_dir"; fi
printf '%s\n' 'Actualizado el icono de chiaki-ng con la versión en español y temas.'
printf 'Copia de seguridad: %s\n' "$backup_dir"
