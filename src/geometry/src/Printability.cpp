#include <nuf/geometry/Printability.hpp>
#include <nuf/geometry/DisplayUnits.hpp>

#include <nuf/geometry/Modeling.hpp>
#include <nuf/geometry/Occt.hpp>

#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Shape.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <sstream>

namespace nuf::geometry {

namespace {

std::string number(double value, int decimals = 2)
{
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(decimals);
    out << value;
    return out.str();
}

double dot(const sketch::Vec3& a, const sketch::Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

bool overlap(const Box& a, const Box& b, double margin)
{
    return !(a.max.x + margin < b.min.x || b.max.x + margin < a.min.x || a.max.y + margin < b.min.y
             || b.max.y + margin < a.min.y || a.max.z + margin < b.min.z || b.max.z + margin < a.min.z);
}

// Aristas que pertenecen a una sola cara: el sólido no es hermético.
bool hasOpenEdges(const Shape& shape)
{
    const ShapeAnalysis_FreeBounds bounds(occtShape(shape), 1e-6, false, false);
    for (const TopoDS_Shape& group : {bounds.GetClosedWires(), bounds.GetOpenWires()}) {
        if (!group.IsNull() && TopExp_Explorer(group, TopAbs_EDGE).More()) {
            return true;
        }
    }
    return false;
}

// Pared más fina entre caras planas paralelas que miran hacia fuera por lados opuestos.
// Devuelve el grosor (0 si no encuentra ninguna por debajo de «below»).
double thinnestWall(const Shape& shape, double below)
{
    struct FacePlane {
        sketch::Vec3 normal;
        double offset = 0.0;  // normal · punto del plano
        Box box;
    };
    std::vector<FacePlane> faces;
    for (int i = 0; i < shape.faceCount(); ++i) {
        if (const auto plane = shape.facePlane(i)) {
            faces.push_back({plane->normal, dot(plane->normal, plane->origin), shape.faceBounds(i)});
        }
    }
    double thinnest = 0.0;
    for (std::size_t i = 0; i < faces.size(); ++i) {
        for (std::size_t j = i + 1; j < faces.size(); ++j) {
            if (dot(faces[i].normal, faces[j].normal) > -0.9999) {
                continue;  // no son paralelas y opuestas
            }
            // Con nj = -ni, la cara j queda detrás de la i (lado del material) a offset_i + offset_j.
            const double thickness = faces[i].offset + faces[j].offset;
            if (thickness <= 1e-6 || thickness >= below) {
                continue;
            }
            if (!overlap(faces[i].box, faces[j].box, thickness + 1e-3)) {
                continue;
            }
            if (thinnest == 0.0 || thickness < thinnest) {
                thinnest = thickness;
            }
        }
    }
    return thinnest;
}

} // namespace

std::vector<PrintIssue> checkPrintable(const std::vector<std::pair<std::string, Shape>>& bodies,
                                       const PrintSettings& settings)
{
    using Level = PrintIssue::Level;
    std::vector<PrintIssue> issues;
    const auto add = [&](Level level, std::string text) { issues.push_back({level, std::move(text)}); };

    constexpr int kMaxFacesForWalls = 1500;
    Box all;
    for (const auto& [name, shape] : bodies) {
        if (shape.isNull() || shape.solidCount() == 0) {
            add(Level::Error, "«" + name + "» no tiene volumen: no se puede imprimir");
            continue;
        }
        if (!shape.isValid()) {
            add(Level::Error, "«" + name + "» no es un sólido válido (el slicer puede fallar)");
        } else if (hasOpenEdges(shape)) {
            add(Level::Error, "«" + name + "» tiene aristas abiertas: no es hermético");
        }
        if (shape.faceCount() > kMaxFacesForWalls) {
            add(Level::Info, "«" + name + "» tiene demasiadas caras para revisar sus paredes");
        } else if (const double wall = thinnestWall(shape, 2.0 * settings.nozzle); wall > 0.0) {
            if (wall < settings.nozzle) {
                add(Level::Warning, "Pared de " + display::length(wall) + " en «" + name + "»: es más fina que tu boquilla ("
                                        + display::length(settings.nozzle, 1) + ") y no se imprimirá");
            } else {
                add(Level::Warning, "Pared de " + display::length(wall) + " en «" + name + "»: menos de dos líneas de boquilla ("
                                        + display::length(2.0 * settings.nozzle, 1) + "), saldrá débil");
            }
        }
        const Box box = shape.bounds();
        if (!box.empty) {
            if (all.empty) {
                all = box;
            } else {
                all.min = {std::min(all.min.x, box.min.x), std::min(all.min.y, box.min.y), std::min(all.min.z, box.min.z)};
                all.max = {std::max(all.max.x, box.max.x), std::max(all.max.y, box.max.y), std::max(all.max.z, box.max.z)};
            }
        }
    }

    // Cuerpos que se solapan: el slicer los mezclará y puede sobrar material.
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        for (std::size_t j = i + 1; j < bodies.size(); ++j) {
            const Shape& a = bodies[i].second;
            const Shape& b = bodies[j].second;
            if (a.isNull() || b.isNull() || !overlap(a.bounds(), b.bounds(), 0.0)) {
                continue;
            }
            try {
                const Shape common = boolean(BooleanOp::Common, a, b);
                if (!common.isNull() && common.volume() > 1e-3) {
                    add(Level::Warning, "«" + bodies[i].first + "» y «" + bodies[j].first + "» se solapan ("
                                            + display::volume(common.volume(), 1) + ")");
                }
            } catch (const GeometryError&) {
                add(Level::Info, "No se pudo comprobar si «" + bodies[i].first + "» y «" + bodies[j].first + "» se solapan");
            }
        }
    }

    // Cabe en la cama (pudiendo girar la pieza sobre sus ejes).
    if (!all.empty) {
        std::array<double, 3> size{all.max.x - all.min.x, all.max.y - all.min.y, all.max.z - all.min.z};
        std::array<double, 3> bed{settings.bedX, settings.bedY, settings.bedZ};
        std::ranges::sort(size);
        std::ranges::sort(bed);
        if (size[0] > bed[0] + 1e-6 || size[1] > bed[1] + 1e-6 || size[2] > bed[2] + 1e-6) {
            add(Level::Warning, "La pieza mide " + display::number(all.max.x - all.min.x, 1) + " × " + display::number(all.max.y - all.min.y, 1) + " × "
                                    + display::length(all.max.z - all.min.z, 1) + " y no cabe en tu cama ("
                                    + display::number(settings.bedX, 0) + " × " + display::number(settings.bedY, 0) + " × " + display::length(settings.bedZ, 0) + ")");
        }
    }
    return issues;
}

} // namespace nuf::geometry
