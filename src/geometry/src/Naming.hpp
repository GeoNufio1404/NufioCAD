#pragma once

// Interno de geometry/: la forma con sus mapas de caras y aristas, y cómo se
// nombran las caras de un resultado a partir de sus entradas.

#include <nuf/geometry/Shape.hpp>

#include <BRepTools_History.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace nuf::geometry {

using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

struct Shape::Impl {
    TopoDS_Shape shape;
    ShapeMap faces;  // índices OCCT desde 1; los públicos, desde 0
    ShapeMap edges;
    std::vector<std::string> faceNames;
    std::vector<std::string> edgeNames;
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
