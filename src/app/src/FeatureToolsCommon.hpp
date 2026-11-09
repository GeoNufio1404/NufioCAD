#pragma once

// Ayudas que comparten los archivos de las herramientas de operación (FeatureTools*.cpp).

#include "FeatureTools.hpp"

#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace nuf::app {


inline constexpr double kDegree = std::numbers::pi / 180.0;

inline QDoubleSpinBox* lengthBox(double value)
{
    auto* box = new ExpressionSpinBox();
    box->setRange(-100000.0, 100000.0);
    box->setDecimals(2);
    box->setValue(value);
    box->setLength(true);
    box->setKeyboardTracking(false);
    return box;
}

inline QDoubleSpinBox* angleBox(double value, double limit)
{
    auto* box = new ExpressionSpinBox();
    box->setRange(-limit, limit);
    box->setDecimals(1);
    setUnit(box, QStringLiteral("°"));
    box->setValue(value);
    box->setKeyboardTracking(false);
    return box;
}

inline QComboBox* combo(std::initializer_list<QString> items)
{
    auto* box = new QComboBox();
    for (const QString& item : items) {
        box->addItem(item);
    }
    return box;
}

// Operación ↔ índice del desplegable («Unir», «Cortar», «Intersecar», «Cuerpo nuevo»).
inline int operationIndex(document::Operation op) { return int(op); }
inline document::Operation operationAt(int index) { return document::Operation(index); }

inline const document::SketchFeature* sketchFeature(const document::Document& doc, document::FeatureId id)
{
    const document::Feature* f = doc.findFeature(id);
    return f && f->kind() == document::FeatureKind::Sketch ? static_cast<const document::SketchFeature*>(f) : nullptr;
}

// El sketch tal como quedó calculado en «state» (con lo proyectado al día),
// o el guardado si no está.
inline const sketch::Sketch* sketchIn(const document::Document& doc, const document::ModelState& state, document::FeatureId id)
{
    if (const auto out = state.sketches.find(id); out != state.sketches.end()) {
        return out->second.sketch.get();
    }
    const document::SketchFeature* sk = sketchFeature(doc, id);
    return sk ? &sk->sketch() : nullptr;
}

// Centro aproximado de un perfil (media de su polígono).
inline sketch::Vec2 profileCenter(const sketch::Profile& profile)
{
    sketch::Vec2 c;
    for (const sketch::Vec2& p : profile.outer.polygon) {
        c.x += p.x;
        c.y += p.y;
    }
    const double n = std::max<double>(1.0, double(profile.outer.polygon.size()));
    return {c.x / n, c.y / n};
}


// El punto «p» movido «t» a lo largo de «dir».
inline sketch::Vec3 moved(const sketch::Vec3& p, const sketch::Vec3& dir, double t)
{
    return {p.x + dir.x * t, p.y + dir.y * t, p.z + dir.z * t};
}

inline sketch::Vec3 cross(const sketch::Vec3& a, const sketch::Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline double dot(const sketch::Vec3& a, const sketch::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline sketch::Vec3 normalized(const sketch::Vec3& v)
{
    const double length = std::sqrt(dot(v, v));
    return length < 1e-12 ? v : sketch::Vec3{v.x / length, v.y / length, v.z / length};
}

inline sketch::Vec3 minus(const sketch::Vec3& a, const sketch::Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

// Una rotación como matriz 3×3 (por filas), para combinar los giros de los anillos.
using Mat3 = std::array<double, 9>;

inline Mat3 multiply(const Mat3& a, const Mat3& b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k) {
                r[std::size_t(3 * i + j)] += a[std::size_t(3 * i + k)] * b[std::size_t(3 * k + j)];
            }
        }
    }
    return r;
}

// Giro de «angle» (rad) alrededor del eje X (0), Y (1) o Z (2) del mundo.
inline Mat3 rotationAbout(int axis, double angle)
{
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    if (axis == 0) {
        return {1, 0, 0, 0, c, -s, 0, s, c};
    }
    if (axis == 1) {
        return {c, 0, s, 0, 1, 0, -s, 0, c};
    }
    return {c, -s, 0, s, c, 0, 0, 0, 1};
}

// Lo que hace la operación Mover con sus tres giros: primero X, luego Y y luego Z (todos sobre los ejes del mundo).
inline Mat3 fromTurns(double x, double y, double z)
{
    return multiply(rotationAbout(2, z), multiply(rotationAbout(1, y), rotationAbout(0, x)));
}

// Los tres giros (X, Y, Z, rad) que dan esa rotación; la inversa de fromTurns.
inline std::array<double, 3> toTurns(const Mat3& r)
{
    const double sinY = std::clamp(-r[6], -1.0, 1.0);
    if (std::abs(sinY) > 0.999999) {
        // Con Y a ±90° los giros X y Z son el mismo: todo el giro va a X.
        return {std::atan2(sinY > 0 ? r[1] : -r[1], r[4]), std::asin(sinY), 0.0};
    }
    return {std::atan2(r[7], r[8]), std::asin(sinY), std::atan2(r[3], r[0])};
}

// El punto «p» girado «angle» (rad) alrededor de la recta que pasa por «center» con dirección «axis» (unitaria).
inline sketch::Vec3 rotatedAbout(const sketch::Vec3& p, const sketch::Vec3& center, const sketch::Vec3& axis, double angle)
{
    const sketch::Vec3 v = minus(p, center);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const sketch::Vec3 k = cross(axis, v);
    const double d = dot(axis, v) * (1.0 - c);
    return {center.x + v.x * c + k.x * s + axis.x * d, center.y + v.y * c + k.y * s + axis.y * d,
            center.z + v.z * c + k.z * s + axis.z * d};
}

} // namespace nuf::app
