#pragma once

// Guardar y abrir un diseño como archivo .nuf. Se guardan los parámetros de
// las operaciones (en JSON), nunca la geometría: al abrir se recalcula el
// historial.

#include <nuf/document/Document.hpp>
#include <nuf/io/NufFile.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace nuf::io {

// Lo que el usuario dejó a la vista; no afecta al modelo pero se recuerda.
struct ViewState {
    std::set<std::string> hiddenBodies;                       // por clave de cuerpo
    std::map<document::FeatureId, bool> sketchVisibility;     // elegida a mano
    bool originVisible = false;
    bool groundVisible = true;

    friend bool operator==(const ViewState&, const ViewState&) = default;
};

// Escribe el diseño. Nunca deja un archivo a medias: se escribe en uno
// temporal junto al destino y luego lo reemplaza; si algo falla, el destino
// queda como estaba. Lanza NufError.
void saveDocument(const std::filesystem::path& path, const document::Document& document, const ViewState& view);

// Abre un diseño en `document` (con un solo recálculo, conservando el objeto).
// Si el archivo está dañado lanza NufError y el documento no cambia. Las
// operaciones que no se pueden calcular quedan marcadas con error, pero el
// archivo se abre igual.
void loadDocument(const std::filesystem::path& path, document::Document& document, ViewState& view);

// El contenido del archivo como JSON legible (para depurar).
std::string dumpDocument(const std::filesystem::path& path);

} // namespace nuf::io
