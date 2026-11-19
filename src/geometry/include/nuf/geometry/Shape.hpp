#pragma once

// Forma B-Rep con nombres estables para sus caras y aristas (solución propia
// al problema de nombrado topológico; ver arquitectura, sección 6).
//
// Cada operación nombra las caras que crea por su origen, por ejemplo
// «f3:lado:e5» (cara lateral que la operación 3 generó a partir de la curva 5
// de su sketch) o «f3:fin:e1» (tapa final del perfil cuyo contorno empieza en
// la curva 1). Las booleanas heredan los nombres de las caras de las que
// vienen. Una arista se nombra por las dos caras que separa: «A|B».
//
// Si varias caras comparten nombre (una cara partida por un corte), se
// distinguen por su orden: de menor a mayor centro (x, luego y, luego z).
//
// No expone tipos de OCCT: geometry/Occt.hpp da acceso a la forma interna.

#include <nuf/sketch/Plane.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nuf::geometry {

// Referencia estable a una cara o arista: nombre y, entre las que lo
// comparten, cuál (0 = la primera).
struct ElementRef {
    std::string name;
    int occurrence = 0;

    bool empty() const { return name.empty(); }
    friend bool operator==(const ElementRef&, const ElementRef&) = default;
};

struct Box {
    sketch::Vec3 min;
    sketch::Vec3 max;
    bool empty = true;
};

class Shape {
public:
    struct Impl;

    Shape() = default;

    bool isNull() const;
    // Sólido(s) cerrados y sin errores topológicos.
    bool isValid() const;
    int solidCount() const;
    double volume() const;
    Box bounds() const;

    // Caras y aristas, numeradas desde 0 (orden estable para la misma forma).
    int faceCount() const;
    const std::string& faceName(int index) const;
    int edgeCount() const;
    const std::string& edgeName(int index) const;

    ElementRef faceRef(int index) const;
    ElementRef edgeRef(int index) const;
    std::optional<int> findFace(const ElementRef& ref) const;
    std::optional<int> findEdge(const ElementRef& ref) const;

    // Las aristas (índices) que rodean una cara.
    std::vector<int> faceEdges(int index) const;

    // Plano de una cara plana, con la normal hacia fuera del sólido; el
    // origen y los ejes, como Plane::fromPointAndNormal.
    std::optional<sketch::Plane> facePlane(int index) const;
    // Centro (de masas) de una cara.
    sketch::Vec3 faceCenter(int index) const;
    // Caja que envuelve una cara.
    Box faceBounds(int index) const;
    // Proyección de una arista sobre un plano, como geometría de su sketch.
    std::vector<sketch::Geometry> projectEdge(int index, const sketch::Plane& plane) const;
    // Recta de una arista recta (punto y dirección unitaria).
    std::optional<std::pair<sketch::Vec3, sketch::Vec3>> edgeLine(int index) const;

    const Impl* impl() const { return impl_.get(); }
    explicit Shape(std::shared_ptr<const Impl> impl);

private:
    std::shared_ptr<const Impl> impl_;
};

// Error de una operación geométrica (perfil abierto, booleana fallida…),
// con un mensaje para el usuario.
class GeometryError : public std::exception {
public:
    explicit GeometryError(std::string message)
        : message_(std::move(message))
    {
    }
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

} // namespace nuf::geometry
