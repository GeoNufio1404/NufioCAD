#pragma once

// Puente con OpenCASCADE para render/ (y el propio geometry/): la forma
// interna de un Shape y las conversiones entre el sketch y OCCT. El resto del
// núcleo no debe incluir esta cabecera.

#include <nuf/geometry/Shape.hpp>
#include <nuf/sketch/Profiles.hpp>

#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include <optional>
#include <vector>

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

// Arista 3D proyectada sobre el plano del sketch, como geometría del sketch:
// líneas y circunferencias paralelas al plano, exactas; el resto, como una
// spline. Vacío si la proyección es un punto.
std::vector<sketch::Geometry> projectEdge(const TopoDS_Edge& edge, const sketch::Plane& plane);

} // namespace nuf::geometry
