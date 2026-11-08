#pragma once

// Ayudas que comparten los archivos de las herramientas de operación (FeatureTools*.cpp).

#include "FeatureTools.hpp"

#include "ExpressionSpinBox.hpp"

#include <nuf/document/Document.hpp>
#include <nuf/document/Features.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>

#include <algorithm>
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


} // namespace nuf::app
