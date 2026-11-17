#include "ViewportImpl.hpp"

// Dibujo del sketch: geometría, cotas, medidas y vista previa de la herramienta.
// Métodos de Viewport::Impl; el estado y las declaraciones están en ViewportImpl.hpp.

namespace nuf::render {

void Viewport::Impl::rebuildProfiles()
{
    if (!shownSketch) {
        return;  // el sketch se cerró mientras esperaba el temporizador
    }
    const std::size_t fingerprint = sketch::geometryFingerprint(*shownSketch);
    if (fingerprint == profilesFingerprint && context->IsDisplayed(profiles) && !profilesPlaneDirty) {
        return;
    }
    profilesFingerprint = fingerprint;
    profilesPlaneDirty = false;
    profilesClock.start();
    profiles->SetShape(profileCompound(*shownSketch, toAx3(sketchPlane)));
    profiles->SetToUpdate();
    if (context->IsDisplayed(profiles)) {
        context->Redisplay(profiles, false);
    } else {
        context->Display(profiles, AIS_Shaded, -1, false);
    }
}

std::string Viewport::Impl::formatMeasure(double value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", value);
    std::string result = text;
    while (!result.empty() && result.back() == '0') {
        result.pop_back();
    }
    if (!result.empty() && result.back() == '.') {
        result.pop_back();
    }
    return result;
}

void Viewport::Impl::addMeasures(const sketch::Sketch& sketch, const std::set<sketch::EntityId>& focus,
                 const Quantity_Color& color, const Quantity_Color& focused)
{
    const Quantity_Color none = color;  // el recuadro no se usa: texto simple
    std::set<sketch::EntityId> dimensioned;
    for (const sketch::Constraint& c : sketch.constraints()) {
        if (c.type == sketch::ConstraintType::Distance || c.type == sketch::ConstraintType::Radius
            || c.type == sketch::ConstraintType::Diameter) {
            dimensioned.insert(c.first.entity);
        }
    }
    const double gap = 14.0 * annotationScale;      // separación de la curva, en mm
    const double minimum = 24.0 * annotationScale;  // una línea más corta no cabe con su texto
    int shown = 0;
    for (const sketch::Entity& e : sketch.entities()) {
        if (e.reference || e.locked || e.construction || dimensioned.contains(e.id)) {
            continue;
        }
        const bool inFocus = focus.contains(e.id);
        if (!measuresAll && !inFocus) {
            continue;
        }
        // Lo enfocado se lee más claro que el resto.
        const Quantity_Color& tone = inFocus ? focused : color;
        if (++shown > 200) {
            break;
        }
        if (const auto* line = std::get_if<sketch::Line>(&e.geometry)) {
            const double dx = line->end.x - line->start.x;
            const double dy = line->end.y - line->start.y;
            const double length = std::hypot(dx, dy);
            if (length < minimum) {
                continue;
            }
            const sketch::Vec2 middle{(line->start.x + line->end.x) / 2.0 - dy / length * gap,
                                      (line->start.y + line->end.y) / 2.0 + dx / length * gap};
            drawing->addText(middle, formatMeasure(length), tone, none, 12.0, false);
        } else if (const auto* circle = std::get_if<sketch::Circle>(&e.geometry)) {
            constexpr double kDiagonal = 0.70710678118654752;
            const double reach = (circle->radius + gap) * kDiagonal;
            drawing->addText({circle->center.x + reach, circle->center.y + reach},
                             "\xC3\x98" + formatMeasure(2.0 * circle->radius), tone, none, 12.0, false);
        } else if (const auto* arc = std::get_if<sketch::Arc>(&e.geometry)) {
            double sweep = arc->endAngle - arc->startAngle;
            while (sweep < 0.0) {
                sweep += 2.0 * std::numbers::pi;
            }
            const double middle = arc->startAngle + sweep / 2.0;
            drawing->addText({arc->center.x + (arc->radius + gap) * std::cos(middle),
                              arc->center.y + (arc->radius + gap) * std::sin(middle)},
                             "R" + formatMeasure(arc->radius), tone, none, 12.0, false);
        }
    }
}

void Viewport::Impl::rebuildSketch()
{
    if (!shownSketch) {
        return;
    }
    const sketch::Sketch& sketch = *shownSketch;
    const std::vector<sketch::EntityId>& underconstrained = shownUnderconstrained;
    const SketchHighlight& highlight = shownHighlight;
    const auto same = [](sketch::Ref a, sketch::Ref b) { return a == b; };
    const auto isSelected = [&](sketch::Ref r) {
        return std::ranges::any_of(highlight.selected, [&](sketch::Ref s) { return same(s, r); });
    };
    // Color de resaltado de una referencia, si lo tiene.
    const auto highlightOf = [&](sketch::Ref r) -> std::optional<Quantity_Color> {
        if (isSelected(r)) {
            return selectedColor();
        }
        if (same(highlight.hovered, r)) {
            return hoveredColor();
        }
        return std::nullopt;
    };

    drawing->clear();
    for (const sketch::Entity& e : sketch.entities()) {
        const auto whole = highlightOf(sketch::Ref::of(e.id));
        if (e.reference) {
            // El origen y los ejes ya los dibuja la rejilla: solo se
            // resaltan (los ejes, como un tramo largo).
            if (!whole) {
                continue;
            }
            if (const auto* axis = std::get_if<sketch::Line>(&e.geometry)) {
                constexpr double kLong = 1e4;
                const sketch::Vec2 d{axis->end.x - axis->start.x, axis->end.y - axis->start.y};
                drawing->addGeometry(sketch::Line{{-kLong * d.x, -kLong * d.y}, {kLong * d.x, kLong * d.y}},
                                     {*whole, 2.0});
            } else if (const auto* origin = std::get_if<sketch::Point>(&e.geometry)) {
                drawing->addPoint(origin->position, *whole, 2.2);
            }
            continue;
        }
        const bool loose = std::ranges::find(underconstrained, e.id) != underconstrained.end();
        SketchStroke stroke = e.construction ? constructionStroke() : (loose ? freeStroke() : fixedStroke());
        if (e.locked && !e.construction) {
            stroke.color = rgb(0xB0, 0x7C, 0xFF);  // bloqueada: violeta, como lo proyectado en Fusion
        }
        if (whole) {
            stroke.color = *whole;
        }
        drawing->addGeometry(e.geometry, stroke);
        // Extremos y centros como puntos (más grandes si están resaltados).
        // (En la geometría bloqueada, como el texto, solo si están resaltados:
        // serían cientos de puntos.)
        const auto mark = [&](sketch::Part which) {
            if (const auto p = sketch.point({e.id, which})) {
                const auto own = highlightOf({e.id, which});
                if (own || !e.locked) {
                    drawing->addPoint(*p, own ? *own : stroke.color, own ? 2.2 : 1.6);
                }
            }
        };
        for (sketch::Part which : {sketch::Part::Start, sketch::Part::End, sketch::Part::Center}) {
            mark(which);
        }
        // Spline seleccionada o bajo el cursor: polígono de control
        // (discontinuo) y polos intermedios.
        const auto* spline = std::get_if<sketch::BSpline>(&e.geometry);
        const bool showPoles = spline && (whole || [&] {
            for (int i = 0; i < int(spline->poles.size()); ++i) {
                if (highlightOf(sketch::Ref::pole(e.id, i))) {
                    return true;
                }
            }
            return false;
        }());
        if (showPoles) {
            drawing->addPolyline(spline->poles, {stroke.color, 1.0, Aspect_TOL_DOT});
            for (int i = 1; i + 1 < int(spline->poles.size()); ++i) {
                const auto own = highlightOf(sketch::Ref::pole(e.id, i));
                drawing->addPoint(spline->poles[std::size_t(i)], own ? *own : stroke.color, own ? 2.0 : 1.3);
            }
        }
    }

    // Restricciones y cotas.
    annotationScale = pixelSize();
    const Quantity_Color text = darkTheme ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29);
    // Texto tenue (medidas automáticas, iconos): 7:1 sobre el fondo claro y 6:1 sobre el oscuro del lienzo.
    const Quantity_Color dimmed = darkTheme ? rgb(0xB0, 0xB8, 0xC2) : rgb(0x3E, 0x48, 0x54);
    const Quantity_Color box = darkTheme ? rgb(0x2B, 0x30, 0x37) : rgb(0xF4, 0xF5, 0xF7);
    // Tres niveles: las cotas que puso el usuario (azul, con sus líneas), las
    // restricciones (iconos pequeños y apagados, solo donde se mira) y las
    // medidas automáticas (texto simple, también solo donde se mira).
    const Quantity_Color accent = darkTheme ? rgb(0x6C, 0xA6, 0xFF) : rgb(0x1F, 0x6F, 0xE0);
    // Lo que tiene el cursor encima o está seleccionado.
    std::set<sketch::EntityId> focus;
    if (!highlight.hovered.empty()) {
        focus.insert(highlight.hovered.entity);
    }
    for (const sketch::Ref& r : highlight.selected) {
        focus.insert(r.entity);
    }
    const auto touchesFocus = [&](const sketch::Constraint& c) {
        return focus.contains(c.first.entity) || focus.contains(c.second.entity) || focus.contains(c.third.entity);
    };
    // Las cotas seleccionadas se colocan primero y ganan los huecos.
    layoutMemory.priority.clear();
    layoutMemory.priority.insert(highlight.selectedConstraints.begin(), highlight.selectedConstraints.end());
    // Los textos de las cotas se añaden al final: así el recuadro de un icono vecino no los tapa.
    struct PendingText {
        sketch::Vec2 at;
        std::string text;
        Quantity_Color color;
        double size;
    };
    std::vector<PendingText> pendingTexts;
    // Lo que mide en píxeles lo que ocupa una lista de puntos (diagonal de su caja).
    const auto diagonalPx = [&](const std::vector<sketch::Vec2>& points) {
        double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
        for (const sketch::Vec2& p : points) {
            x0 = std::min(x0, p.x);
            x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y);
            y1 = std::max(y1, p.y);
        }
        return x1 < x0 ? 1e300 : std::hypot(x1 - x0, y1 - y0) / annotationScale;
    };
    // Lo que mide la anotación: la línea de cota si la hay, si no lo que ocupan sus líneas.
    const auto extentPx = [&](const sketch::Annotation& note) {
        if (note.lines.size() >= 3) {
            if (const auto* base = std::get_if<sketch::Line>(&note.lines[2])) {
                return std::hypot(base->end.x - base->start.x, base->end.y - base->start.y) / annotationScale;
            }
        }
        std::vector<sketch::Vec2> points;
        for (const sketch::Geometry& g : note.lines) {
            for (const sketch::Vec2& p : sketch::polyline(g)) {
                points.push_back(p);
            }
        }
        return diagonalPx(points);
    };
    for (const sketch::Annotation& note : sketch::layoutAnnotations(sketch, annotationScale, &layoutMemory)) {
        const bool picked =
            std::ranges::find(highlight.selectedConstraints, note.constraint) != highlight.selectedConstraints.end();
        const bool hovered = note.constraint == highlight.hoveredConstraint;
        const bool pinned = std::ranges::find(highlight.pinnedConstraints, note.constraint) != highlight.pinnedConstraints.end();
        bool relevant = note.dimension || constraintsAll || picked || hovered || pinned;
        if (!relevant) {
            for (const sketch::Constraint& c : sketch.constraints()) {
                if (c.id == note.constraint) {
                    relevant = touchesFocus(c);
                    break;
                }
            }
        }
        if (!relevant) {
            continue;
        }
        // Alejándose, las cotas (y los iconos) de lo que ya se ve diminuto se ocultan, como las medidas sueltas,
        // y vuelven al acercarse. Lo seleccionado, señalado o fijado se queda.
        if (!picked && !hovered && !pinned) {
            double size = 1e300;
            if (note.dimension) {
                size = extentPx(note);
            } else {
                for (const sketch::Constraint& c : sketch.constraints()) {
                    if (c.id == note.constraint) {
                        if (const sketch::Entity* e = sketch.entity(c.first.entity)) {
                            size = diagonalPx(sketch::polyline(e->geometry));
                        }
                        break;
                    }
                }
            }
            if (size < 24.0) {
                continue;
            }
        }
        Quantity_Color color = note.dimension ? (note.reference ? dimmed : accent) : dimmed;
        if (picked) {
            color = selectedColor();
        } else if (hovered) {
            color = hoveredColor();
        }
        for (const sketch::Geometry& line : note.lines) {
            drawing->addGeometry(line, {color, 1.0});
        }
        // Las cotas, con su recuadro y tamaño normal; las restricciones, más pequeñas.
        if (!note.icon.empty()) {
            // Fondo opaco (un recuadro del color del lienzo, con la letra del mismo color para que no se vea)
            // para que el icono se lea aunque cruce una línea; el icono se dibuja encima del recuadro.
            drawing->addText(note.anchor, "M", box, box, 11.0);
            for (const sketch::Geometry& part : note.icon) {
                drawing->addGeometryOver(part, {color, 1.6});
            }
        } else {
            pendingTexts.push_back({note.anchor, note.text, color, note.dimension ? 13.0 : 11.0});
        }
    }
    for (const PendingText& pending : pendingTexts) {
        drawing->addText(pending.at, pending.text, pending.color, box, pending.size);
    }
    if (measuresVisible) {
        addMeasures(sketch, focus, dimmed, text);
    }
    context->Redisplay(drawing, false);
}

void Viewport::Impl::showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint,
                       const std::vector<std::pair<sketch::Vec2, sketch::Vec2>>& guides,
                       const std::string& badge, sketch::Vec2 badgeAt, bool markerSquare,
                       const std::vector<sketch::Vec2>& awake)
{
    preview->clear();
    if (!awake.empty()) {
        // Anillo amarillo (7 px) en cada punto despierto.
        SketchStroke ring = freeStroke();
        ring.color = rgb(0xFF, 0xC9, 0x40);
        ring.width = 1.5;
        for (const sketch::Vec2& at : awake) {
            preview->addGeometry(sketch::Circle{at, 7.0 * pixelSize()}, ring);
        }
    }
    if (snapPoint && markerSquare) {
        // Cuadrado naranja discontinuo (7 px de semilado) alrededor del punto.
        const double h = 7.0 * pixelSize();
        const sketch::Vec2 c = *snapPoint;
        const sketch::Vec2 corners[4] = {{c.x - h, c.y - h}, {c.x + h, c.y - h}, {c.x + h, c.y + h}, {c.x - h, c.y + h}};
        for (int i = 0; i < 4; ++i) {
            preview->addGeometry(sketch::Line{corners[i], corners[(i + 1) % 4]}, constructionStroke());
        }
    }
    if (!badge.empty()) {
        // Los símbolos conocidos se dibujan con líneas (sin depender de la fuente), algo más grandes.
        std::vector<sketch::Geometry> drawn;
        if (badge == "∥") {
            drawn = sketch::glyphGeometry(sketch::ConstraintType::Parallel, badgeAt, 1.4 * pixelSize());
        } else if (badge == "⊥") {
            drawn = sketch::glyphGeometry(sketch::ConstraintType::Perpendicular, badgeAt, 1.4 * pixelSize());
        } else if (badge == "T") {
            drawn = sketch::glyphGeometry(sketch::ConstraintType::Tangent, badgeAt, 1.4 * pixelSize());
        }
        if (drawn.empty()) {
            preview->addText(badgeAt, badge, rgb(0xF2, 0xA3, 0x4B), rgb(0x1F, 0x23, 0x29), 16.0);
        } else {
            SketchStroke orange = freeStroke();
            orange.color = rgb(0xF2, 0xA3, 0x4B);
            orange.width = 2.0;
            for (const sketch::Geometry& part : drawn) {
                preview->addGeometry(part, orange);
            }
        }
    }
    for (const auto& [from, to] : guides) {
        preview->addGeometry(sketch::Line{from, to}, constructionStroke());
    }
    SketchStroke stroke = freeStroke();
    stroke.width = 1.5;
    for (const sketch::Geometry& g : geometry) {
        preview->addGeometry(g, stroke);
    }
    if (snapPoint) {
        preview->addPoint(*snapPoint, rgb(0xFF, 0xC9, 0x40), 3.0);
    }
    context->Redisplay(preview, false);
}

double Viewport::Impl::pixelSize() const
{
    double width = 0, height = 0;
    view->Size(width, height);
    int pixelsWide = 0, pixelsHigh = 0;
    view->Window()->Size(pixelsWide, pixelsHigh);
    return width / std::max(pixelsWide, 1);
}

} // namespace nuf::render
