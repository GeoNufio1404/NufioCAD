#pragma once

// Plano en el que vive un sketch, en coordenadas del modelo (mm, Z arriba).

#include <nuf/sketch/Sketch.hpp>

namespace nuf::sketch {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Sistema de ejes ortonormal y a derechas: xAxis × yAxis = normal. La normal
// apunta hacia quien mira el sketch de frente.
struct Plane {
    Vec3 origin;
    Vec3 xAxis{1.0, 0.0, 0.0};
    Vec3 yAxis{0.0, 1.0, 0.0};
    Vec3 normal{0.0, 0.0, 1.0};

    // Planos de origen, con la orientación de Fusion 360 (Z arriba):
    // XY = planta, XZ = alzado frontal, YZ = lateral derecho.
    static Plane xy();
    static Plane xz();
    static Plane yz();

    // Plano que pasa por un punto con esa normal (por ejemplo, una cara).
    // El origen del sketch es la proyección del origen del modelo, y el eje
    // Y es el Z del modelo proyectado (o el Y si el plano es horizontal),
    // para que la vista de frente quede «derecha».
    // Lanza std::invalid_argument si la normal es nula.
    static Plane fromPointAndNormal(Vec3 point, Vec3 normal);

    Vec3 toWorld(Vec2 p) const;
    // Proyecta el punto sobre el plano y devuelve sus coordenadas.
    Vec2 toLocal(Vec3 p) const;
};

} // namespace nuf::sketch
