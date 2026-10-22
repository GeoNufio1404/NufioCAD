#pragma once

// Operaciones de modelado: extruir, revolucionar y booleanas, con nombres
// para las caras que crean (ver Shape.hpp). Lanzan GeometryError si fallan.
//
// «tag» identifica la operación del documento que llama (ej. "f3") y es el
// prefijo de los nombres de las caras nuevas.

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <string_view>
#include <vector>

namespace nuf::geometry {

// Perfiles cerrados de un sketch en su plano.
struct SketchRegion {
    const sketch::Sketch* sketch = nullptr;
    sketch::Plane plane;
    std::vector<sketch::Profile> profiles;
};

// Hasta dónde llega un lado de la extrusión.
enum class Extent {
    Distance,    // «distance» mm
    ThroughAll,  // atraviesa todos los cuerpos de «context»
    ToFace,      // hasta el plano de una cara plana de «context»
};

struct ExtrudeSide {
    Extent extent = Extent::Distance;
    double distance = 10.0;  // mm; en una dirección puede ser negativa (invierte)
    double taper = 0.0;      // radianes; positivo = se ensancha
    ElementRef face;         // con Extent::ToFace
};

enum class ExtrudeDirection {
    One,        // solo side1, a lo largo de la normal del plano
    Symmetric,  // side1 hacia los dos lados
    Two,        // side1 a lo largo de la normal y side2 en contra
};

struct ExtrudeSpec {
    ExtrudeDirection direction = ExtrudeDirection::One;
    ExtrudeSide side1;
    ExtrudeSide side2;
    double startOffset = 0.0;  // mm a lo largo de la normal
};

// Sólido extruido de los perfiles. Las caras laterales se llaman
// «tag:lado:e<curva>», las tapas «tag:inicio:e<k>» y «tag:fin:e<k>», con k la
// menor curva del contorno exterior del perfil.
Shape extrude(std::string_view tag, const SketchRegion& region, const ExtrudeSpec& spec,
              const std::vector<Shape>& context = {});

// Extruye una cara plana de un cuerpo (empujar/tirar); los laterales se
// llaman «tag:lado:<nombre de la arista>».
Shape extrudeFace(std::string_view tag, const Shape& body, int faceIndex, const ExtrudeSpec& spec,
                  const std::vector<Shape>& context = {});

struct Axis {
    sketch::Vec3 point;
    sketch::Vec3 direction{0.0, 0.0, 1.0};
};

enum class RevolveExtent {
    Angle,  // side1 grados en una dirección (o simétrico / dos lados)
    Full,   // vuelta completa
};

struct RevolveSpec {
    RevolveExtent extent = RevolveExtent::Angle;
    ExtrudeDirection direction = ExtrudeDirection::One;
    double angle1 = 3.14159265358979323846;  // radianes; puede ser negativo
    double angle2 = 0.0;
};

// Sólido de revolución de los perfiles alrededor del eje (que no debe
// cruzarlos). Mismos nombres que extrude().
Shape revolve(std::string_view tag, const SketchRegion& region, const Axis& axis, const RevolveSpec& spec);

enum class BooleanOp { Fuse, Cut, Common };

// target ∘ tool, con las caras coplanares unidas. Puede devolver una forma
// nula (un corte que se lleva todo el cuerpo).
Shape boolean(BooleanOp op, const Shape& target, const Shape& tool);

// true si las formas se tocan o se solapan (para elegir con qué unir).
bool touches(const Shape& a, const Shape& b);

} // namespace nuf::geometry
