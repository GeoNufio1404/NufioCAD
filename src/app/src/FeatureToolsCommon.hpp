#pragma once

// Ayudas que comparten los archivos de las herramientas de operación (FeatureTools*.cpp).

#include "FeatureTools.hpp"

#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>

#include <algorithm>
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
