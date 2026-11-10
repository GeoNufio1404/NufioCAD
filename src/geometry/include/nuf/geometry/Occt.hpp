#pragma once

// Puente con OpenCASCADE para render/ (y el propio geometry/): la forma
// interna de un Shape y las conversiones entre el sketch y OCCT. El resto del
// núcleo no debe incluir esta cabecera.

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <Standard_Handle.hxx>
#include <Standard_Version.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

// OCCT 8 trae `occ::handle<T>`; OCCT 7.x (Fedora) solo `opencascade::handle<T>`.
// Con este alias el mismo código compila con las dos.
#if OCC_VERSION_HEX < 0x080000
namespace occ {
using opencascade::handle;
}
#endif

// Arrays con handle: `NCollection_HArray1<T>` en OCCT 8; en 7.x, las clases
// con nombre TColgp_/TColStd_.
#if OCC_VERSION_HEX >= 0x080000
#include <NCollection_HArray1.hxx>
namespace nuf::geometry {
using HPntArray = NCollection_HArray1<gp_Pnt>;
using HRealArray = NCollection_HArray1<double>;
} // namespace nuf::geometry
#else
#include <TColStd_HArray1OfReal.hxx>
#include <TColgp_HArray1OfPnt.hxx>
namespace nuf::geometry {
using HPntArray = TColgp_HArray1OfPnt;
using HRealArray = TColStd_HArray1OfReal;
} // namespace nuf::geometry
#endif

namespace nuf::geometry {

// Forma OCCT de un Shape (nula si lo es).
const TopoDS_Shape& occtShape(const Shape& shape);
// Índice de una cara o arista de la forma (nullopt si no es suya).
std::optional<int> faceIndexOf(const Shape& shape, const TopoDS_Shape& face);
std::optional<int> edgeIndexOf(const Shape& shape, const TopoDS_Shape& edge);
const TopoDS_Face& faceAt(const Shape& shape, int index);
const TopoDS_Edge& edgeAt(const Shape& shape, int index);

inline gp_Pnt toPnt(const sketch::Vec3& v) { return gp_Pnt(v.x, v.y, v.z); }
inline gp_Dir toDir(const sketch::Vec3& v) { return gp_Dir(v.x, v.y, v.z); }
inline sketch::Vec3 toVec3(const gp_XYZ& v) { return {v.X(), v.Y(), v.Z()}; }
gp_Ax3 toAx3(const sketch::Plane& plane);

// Arista OCCT de una curva del sketch (nullopt si es un punto o falla).
std::optional<TopoDS_Edge> sketchEdge(const sketch::Entity& entity, const gp_Ax3& plane);

// De qué curva del sketch viene cada arista de una cara de perfil.
struct EdgeOrigin {
    TopoDS_Edge edge;
    sketch::EntityId entity = 0;
};

// Cara plana de un perfil cerrado (con sus agujeros); nullopt si no se pudo.
std::optional<TopoDS_Face> profileFace(const sketch::Sketch& sketch, const sketch::Profile& profile,
                                       const gp_Ax3& plane, std::vector<EdgeOrigin>* origins = nullptr);
// Una cara por cada perfil cerrado del sketch.
std::vector<TopoDS_Face> profileFaces(const sketch::Sketch& sketch, const gp_Ax3& plane);

// Contorno limpio de unas regiones del plano XY (como las letras de un
// texto): los contornos del sentido principal se suman y los del contrario
// (los huecos) se restan, así que los que se solapan quedan en uno. Devuelve
// las curvas del resultado como geometría de sketch (en XY).
std::vector<sketch::Geometry> outlineOf(const TopoDS_Shape& planarShape);

// Arista 3D proyectada sobre el plano del sketch, como geometría del sketch:
// líneas y circunferencias paralelas al plano, exactas; el resto, como una
// spline. Vacío si la proyección es un punto.
std::vector<sketch::Geometry> projectEdge(const TopoDS_Edge& edge, const sketch::Plane& plane);

} // namespace nuf::geometry
