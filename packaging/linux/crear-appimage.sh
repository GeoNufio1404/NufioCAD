#!/usr/bin/env bash
# Crea el AppImage de NufioCAD (Fase 19, D1): un solo archivo que corre en casi cualquier distribución de escritorio.
# Uso: packaging/linux/crear-appimage.sh [carpeta de compilación, por defecto build/linux-release]
# Necesita: la compilación hecha, Qt 6 con qmake6, FUSE 2 (o APPIMAGE_EXTRACT_AND_RUN=1) y salida a internet la primera vez
# (baja linuxdeploy y su complemento de Qt a build/herramientas-appimage). Deja NufioCAD-<versión>-x86_64.AppImage en la
# carpeta actual. SIN PROBAR todavía en una distribución real (se escribió el 19 nov. 2026 desde Windows).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="$(realpath "${1:-$root/build/linux-release}")"
tools="$root/build/herramientas-appimage"
appdir="$root/build/NufioCAD.AppDir"

[[ -x "$build/src/app/NufioCAD" ]] || { echo "No hay compilación en $build (falta src/app/NufioCAD)." >&2; exit 1; }
version="$(grep -m1 'CMAKE_PROJECT_VERSION:' "$build/CMakeCache.txt" | cut -d= -f2)"
version="${version:-0.0.0}"

mkdir -p "$tools"
fetch() { # fetch <archivo> <url>
    [[ -x "$tools/$1" ]] || { curl -fL --retry 3 -o "$tools/$1" "$2"; chmod +x "$tools/$1"; }
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20250213-2/linuxdeploy-x86_64.AppImage
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage

rm -rf "$appdir"
cmake --install "$build" --prefix "$appdir/usr"

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export APPIMAGE_EXTRACT_AND_RUN=1          # sin FUSE (los servidores de CI no lo tienen)
export LINUXDEPLOY_OUTPUT_VERSION="$version"
export PATH="$tools:$PATH"

"$tools/linuxdeploy-x86_64.AppImage" \
    --appdir "$appdir" \
    --executable "$appdir/usr/bin/NufioCAD" \
    --desktop-file "$root/packaging/linux/nufiocad.desktop" \
    --icon-file "$root/src/app/resources/icons/app/nufiocad-256.png" \
    --plugin qt \
    --output appimage

ls -1 NufioCAD*.AppImage
