#include <nuf/sketch/FromSelection.hpp>

#include <cmath>

namespace nuf::sketch {

namespace {

// Selección clasificada en puntos y curvas, conservando el orden.
struct Classified {
    std::vector<Ref> points;
    std::vector<Ref> curves;
    std::vector<Ref> lines;
    std::vector<Ref> rounds;  // círculos y arcos
};

Classified classify(const Sketch& sketch, const std::vector<Ref>& selection)
{
    Classified c;
    for (const Ref& r : selection) {
        if (sketch.point(r)) {
            c.points.push_back(r);
            continue;
        }
        const Entity* e = sketch.entity(r.entity);
        if (!e || r.part != Part::Whole) {
            continue;
        }
        if (std::holds_alternative<Line>(e->geometry)) {
            c.curves.push_back(r);
            c.lines.push_back(r);
        } else if (std::holds_alternative<Circle>(e->geometry) || std::holds_alternative<Arc>(e->geometry)) {
            c.curves.push_back(r);
            c.rounds.push_back(r);
        }
    }
    return c;
}

// La primera con cada una de las demás.
std::vector<Constraint> pairwise(const std::vector<Ref>& refs, Constraint (*make)(EntityId, EntityId))
{
    std::vector<Constraint> result;
    for (std::size_t i = 1; i < refs.size(); ++i) {
        result.push_back(make(refs[0].entity, refs[i].entity));
    }
    return result;
}

} // namespace

std::vector<Constraint> constraintsFromSelection(ConstraintType type, const Sketch& sketch,
                                                 const std::vector<Ref>& selection)
{
    const Classified c = classify(sketch, selection);
    const std::size_t total = selection.size();
    std::vector<Constraint> result;

    switch (type) {
    case ConstraintType::Coincident:
        if (total == 2 && c.points.size() == 2) {
            result.push_back(Constraint::coincident(c.points[0], c.points[1]));
        } else if (total == 2 && c.points.size() == 1 && c.curves.size() == 1) {
            result.push_back(Constraint::pointOnObject(c.points[0], c.curves[0].entity));
        }
        break;
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical: {
        const bool horizontal = type == ConstraintType::Horizontal;
        if (!c.lines.empty() && c.lines.size() == total) {
            for (const Ref& line : c.lines) {
                result.push_back(horizontal ? Constraint::horizontal(line.entity) : Constraint::vertical(line.entity));
            }
        } else if (total == 2 && c.points.size() == 2) {
            result.push_back(horizontal ? Constraint::horizontal(c.points[0], c.points[1])
                                        : Constraint::vertical(c.points[0], c.points[1]));
        }
        break;
    }
    case ConstraintType::Parallel:
        if (c.lines.size() >= 2 && c.lines.size() == total) {
            result = pairwise(c.lines, &Constraint::parallel);
        }
        break;
    case ConstraintType::Collinear:
        if (c.lines.size() >= 2 && c.lines.size() == total) {
            result = pairwise(c.lines, &Constraint::collinear);
        }
        break;
    case ConstraintType::Perpendicular:
        if (total == 2 && c.lines.size() == 2) {
            result.push_back(Constraint::perpendicular(c.lines[0].entity, c.lines[1].entity));
        }
        break;
    case ConstraintType::Equal:
        if (total >= 2 && (c.lines.size() == total || c.rounds.size() == total)) {
            result = pairwise(c.curves, &Constraint::equal);
        }
        break;
    case ConstraintType::Tangent:
        if (total == 2 && c.curves.size() == 2) {
            result.push_back(Constraint::tangent(c.curves[0].entity, c.curves[1].entity));
        } else if (total == 2 && c.points.size() == 2) {
            // Dos extremos: tangencia en el punto de unión.
            result.push_back(Constraint::tangentAt(c.points[0], c.points[1]));
        }
        break;
    case ConstraintType::Concentric:
        if (total == 2 && c.rounds.size() == 2) {
            result.push_back(Constraint::concentric(c.rounds[0].entity, c.rounds[1].entity));
        }
        break;
    case ConstraintType::Midpoint:
        if (total == 2 && c.points.size() == 1 && c.lines.size() == 1) {
            result.push_back(Constraint::midpoint(c.points[0], c.lines[0].entity));
        }
        break;
    case ConstraintType::Symmetric:
        if (total == 3 && c.points.size() == 2 && c.lines.size() == 1) {
            result.push_back(Constraint::symmetric(c.points[0], c.points[1], c.lines[0]));
        } else if (total == 3 && c.points.size() == 3) {
            result.push_back(Constraint::symmetric(c.points[0], c.points[1], c.points[2]));
        }
        break;
    case ConstraintType::Fix:
        for (const Ref& r : selection) {
            result.push_back(Constraint::fix(r));
        }
        break;
    default:
        break;  // las cotas van por dimensionFromSelection
    }

    for (const Constraint& constraint : result) {
        if (!sketch.canAdd(constraint)) {
            return {};
        }
    }
    return result;
}

std::optional<Constraint> dimensionFromSelection(const Sketch& sketch, const std::vector<Ref>& selection)
{
    const Classified c = classify(sketch, selection);
    const std::size_t total = selection.size();
    std::optional<Constraint> result;

    if (total == 1 && c.lines.size() == 1) {
        result = Constraint::length(c.lines[0].entity, 0.0);
    } else if (total == 1 && c.rounds.size() == 1) {
        const bool isArc = std::holds_alternative<Arc>(sketch.entity(c.rounds[0].entity)->geometry);
        result = isArc ? Constraint::radius(c.rounds[0].entity, 1.0) : Constraint::diameter(c.rounds[0].entity, 1.0);
    } else if (total == 2 && c.points.size() == 2) {
        result = Constraint::distance(c.points[0], c.points[1], 0.0);
    } else if (total == 2 && c.points.size() == 1 && c.lines.size() == 1) {
        result = Constraint::distance(c.points[0], c.lines[0], 0.0);
    } else if (total == 2 && c.lines.size() == 2) {
        const auto& a = std::get<Line>(sketch.entity(c.lines[0].entity)->geometry);
        const auto& b = std::get<Line>(sketch.entity(c.lines[1].entity)->geometry);
        const double cross = (a.end.x - a.start.x) * (b.end.y - b.start.y) - (a.end.y - a.start.y) * (b.end.x - b.start.x);
        const double lengths = std::hypot(a.end.x - a.start.x, a.end.y - a.start.y)
                               * std::hypot(b.end.x - b.start.x, b.end.y - b.start.y);
        if (std::abs(cross) <= 1e-9 * lengths) {
            // Paralelas: distancia de un extremo de la segunda a la primera.
            result = Constraint::distance(Ref::start(c.lines[1].entity), c.lines[0], 0.0);
        } else {
            result = Constraint::angle(c.lines[0].entity, c.lines[1].entity, 0.0);
        }
    }

    if (!result) {
        return std::nullopt;
    }
    result->value = sketch.measure(*result);
    if (!sketch.canAdd(*result)) {
        return std::nullopt;
    }
    return result;
}

} // namespace nuf::sketch
