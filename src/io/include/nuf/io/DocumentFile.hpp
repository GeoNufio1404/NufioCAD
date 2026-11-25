#pragma once

// Guardar y abrir un diseño como archivo .nuf. Se guardan los parámetros de
// las operaciones (en JSON), nunca la geometría: al abrir se recalcula el
// historial.

#include <nuf/document/Document.hpp>
#include <nuf/io/NufFile.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace nuf::io {

// Lo que el usuario dejó a la vista; no afecta al modelo pero se recuerda.
struct ViewState {
    std::set<std::string> hiddenBodies;                       // por clave de cuerpo
    std::map<document::FeatureId, bool> sketchVisibility;     // elegida a mano
    bool originVisible = false;
    bool groundVisible = true;
    // Unidad con que se mostraban las medidas al guardar («mm», «cm», «m», «in»); vacía en los archivos anteriores.
    // Es de visualización: no cuenta para decidir si hay cambios sin guardar.
    std::string displayUnit;
    // La cámara al guardar (O6): ojo (3), centro (3), arriba (3), escala y 1 si es ortogonal; vacía en los archivos anteriores. Es de
    // visualización: no cuenta para decidir si hay cambios sin guardar.
    std::vector<double> camera;
    // Solo al abrir: un aviso para quien lo abre (el contenido no coincide con su huella). No se guarda ni cuenta como cambio.
    std::string warning;

    friend bool operator==(const ViewState& a, const ViewState& b)
    {
        return a.hiddenBodies == b.hiddenBodies && a.sketchVisibility == b.sketchVisibility &&
               a.originVisible == b.originVisible && a.groundVisible == b.groundVisible;
    }
};

// Todo lo que se escribe en un archivo, ya armado (JSON de cada operación, parámetros y los BREP importados, que se
// comparten sin copiar). Se arma en el hilo que tiene el documento (es rápido) y se escribe en cualquier otro: así
// guardar no obliga a la interfaz a esperar a SQLite, al disco ni a OneDrive. Es independiente del documento: puede
// seguir cambiando mientras se escribe.
struct SavePlan {
    struct Impl;
    std::shared_ptr<const Impl> impl;
};

SavePlan prepareSave(const document::Document& document, const ViewState& view, const std::string& thumbnailPng = {});
// Escribe un plan con las mismas garantías que saveDocument. Lanza NufError.
void writeSave(const std::filesystem::path& path, const SavePlan& plan);

// Escribe el diseño (prepareSave y writeSave seguidos). Nunca deja un archivo a medias: se escribe en uno
// temporal junto al destino y luego lo reemplaza; si algo falla, el destino
// queda como estaba. `thumbnailPng` (opcional) es la miniatura que verá la
// lista de recientes. Lanza NufError.
void saveDocument(const std::filesystem::path& path, const document::Document& document, const ViewState& view,
                  const std::string& thumbnailPng = {});

// La miniatura guardada en el archivo, sin abrir el diseño ni recalcularlo.
// nullopt si no tiene o el archivo no se puede leer.
std::optional<std::string> readThumbnail(const std::filesystem::path& path) noexcept;

// Abre un diseño en `document` (con un solo recálculo, conservando el objeto).
// Si el archivo está dañado lanza NufError y el documento no cambia. Las
// operaciones que no se pueden calcular quedan marcadas con error, pero el
// archivo se abre igual.
void loadDocument(const std::filesystem::path& path, document::Document& document, ViewState& view);

// El contenido del archivo como JSON legible (para depurar).
std::string dumpDocument(const std::filesystem::path& path);

} // namespace nuf::io
