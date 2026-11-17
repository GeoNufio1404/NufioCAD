#pragma once

#include <atomic>

// Interno de geometry/: la forma con sus mapas de caras y aristas, y cómo se
// nombran las caras de un resultado a partir de sus entradas.

#include <nuf/geometry/Shape.hpp>

#include <BRepTools_History.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nuf::geometry {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct Shape::Impl {
    TopoDS_Shape shape;
    ShapeMap faces;  // índices OCCT desde 1; los públicos, desde 0
    ShapeMap edges;
    std::vector<std::string> faceNames;
    std::vector<std::string> edgeNames;
    // Resultados guardados para no recorrer todas las caras en cada refresco del visor (la forma no cambia).
    mutable std::atomic<signed char> heavy{-1};  // hasHeavySurface: -1 sin calcular, 0 no, 1 sí
    mutable std::atomic<bool> meshed{false};     // hasMesh: una vez mallada, sigue mallada

    // Consultas guardadas (la forma no cambia): los índices de cada nombre, ya ordenados por posición, y la caja. Antes
    // cada búsqueda recorría todos los nombres y, con nombres repetidos (patrones, caras partidas), volvía a calcular el
    // centro de masa de cada cara repetida; se pide en cada arista de un redondeo, en cada sketch sobre una cara, al
    // medir… Se arman la primera vez que se piden (con candado: la forma se comparte entre hilos).
    struct NameIndex {
        bool built = false;
        std::unordered_map<std::string, std::vector<int>> byName;  // índices 0-based; los repetidos, por posición
    };
    mutable std::mutex cacheMutex;
    mutable NameIndex faceIndex;
    mutable NameIndex edgeIndex;
    mutable std::optional<Box> boundsCache;
};

// Reúne los nombres de las caras de un resultado.
class Namer {
public:
    explicit Namer(const TopoDS_Shape& result);

    // Nombra la cara (o todas las caras de la forma) si es del resultado. Si
    // ya tenía nombre, gana el menor (para que sea determinista).
    void name(const TopoDS_Shape& faces, const std::string& name);

    // Las caras de «input» pasan su nombre a las que salen de ellas en el
    // resultado (modificadas o intactas).
    void inherit(const Shape& input, const BRepTools_History& history);
    // Igual, sin historial: las que siguen siendo las mismas.
    void inherit(const Shape& input);
    // Nombra las caras que aún no tienen nombre.
    void nameUnnamed(const std::string& name);

    // Las caras sin nombre se llaman «tag:?<n>»; las aristas se nombran por
    // sus caras.
    Shape finish(const std::string& fallbackTag) const;

private:
    TopoDS_Shape result_;
    ShapeMap faces_;
    std::vector<std::string> names_;
};

} // namespace nuf::geometry
