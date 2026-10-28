#!/usr/bin/env bash
# Registra NufioCAD en el escritorio del usuario (sin permisos de administrador):
# entrada en el menú de aplicaciones, icono en el dock y asociación de .nuf.
# Uso: packaging/linux/install-local.sh [ruta/al/ejecutable NufioCAD]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
binary="$(realpath "${1:-$root/build/linux-debug/src/app/NufioCAD}")"
[[ -x "$binary" ]] || { echo "No existe el ejecutable: $binary" >&2; exit 1; }

data="${XDG_DATA_HOME:-$HOME/.local/share}"
mkdir -p "$data/applications" "$data/mime/packages"

# Iconos en cada tamaño que trae la aplicación.
for png in "$root"/src/app/resources/icons/app/nufiocad-*.png; do
    size="${png##*-}"; size="${size%.png}"
    dir="$data/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    cp "$png" "$dir/nufiocad.png"
done

# El .desktop apunta al ejecutable indicado.
sed "s|^Exec=NufioCAD|Exec=$binary|" "$root/packaging/linux/nufiocad.desktop" > "$data/applications/nufiocad.desktop"
cp "$root/packaging/linux/nufiocad-mime.xml" "$data/mime/packages/nufiocad.xml"

command -v update-mime-database >/dev/null && update-mime-database "$data/mime" || true
command -v update-desktop-database >/dev/null && update-desktop-database "$data/applications" || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t "$data/icons/hicolor" || true

echo "Instalado para este usuario con Exec=$binary"
echo "Si el dock sigue mostrando el icono genérico, cierra y vuelve a abrir NufioCAD."
