#include "ViewportImpl.hpp"

// Flechas de distancia y medida, gizmo de mover y girar, fantasma y líneas de ayuda.
// Métodos de Viewport::Impl; el estado y las declaraciones están en ViewportImpl.hpp.

namespace nuf::render {

void Viewport::Impl::showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance)
{
    // Se redibuja en cada paso de un arrastre (la vista previa lo pide al
    // cambiar la distancia): no se debe cortar el arrastre en curso.
    handleOrigin = toPnt(origin);
    handleDirection = toDir(direction);
    handleDistance = distance;
    // Tamaño fijo en pantalla (aprox.): se recalcula al volver a mostrarla.
    const double size = 70.0 * pixelSize();
    const bool positive = distance >= 0.0;
    const gp_Pnt tip = handleOrigin.Translated(gp_Vec(handleDirection) * distance);
    TopLoc_Location place;
    {
        gp_Trsf move;
        move.SetTranslation(gp_Vec(gp_Pnt(0.0, 0.0, 0.0), tip));
        place = TopLoc_Location(move);
    }
    // En un arrastre la flecha solo cambia de lugar: se reutiliza el sólido
    // (crearlo y mostrarlo de nuevo en cada movimiento del ratón era lo más
    // caro del arrastre) y se reconstruye únicamente si cambia su forma.
    if (!handle.IsNull() && positive == handlePositive && handleBuiltDirection.IsEqual(handleDirection, 1e-9) &&
        std::abs(size - handleBuiltSize) < 0.02 * handleBuiltSize) {
        context->SetLocation(handle, place);
        return;
    }
    removeHandle();
    handlePositive = positive;
    handleBuiltDirection = handleDirection;
    handleBuiltSize = size;
    // La flecha se construye con la punta en el origen y se coloca con
    // la ubicación local.
    const gp_Dir pointing = positive ? handleDirection : handleDirection.Reversed();
    const gp_Pnt base = gp_Pnt(0.0, 0.0, 0.0).Translated(gp_Vec(pointing) * (0.15 * size));
    BRep_Builder builder;
    TopoDS_Compound arrow;
    builder.MakeCompound(arrow);
    builder.Add(arrow, BRepPrimAPI_MakeCylinder(gp_Ax2(base, pointing), 0.035 * size, 0.6 * size).Shape());
    builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(base.Translated(gp_Vec(pointing) * (0.6 * size)), pointing),
                                            0.12 * size, 0.0, 0.3 * size)
                           .Shape());
    handle = new AIS_Shape(arrow);
    handle->SetColor(rgb(0xF2, 0xA3, 0x4B));
    handle->Attributes()->SetFaceBoundaryDraw(false);
    handle->SetZLayer(Graphic3d_ZLayerId_Topmost);
    handle->SetLocalTransformation(place.Transformation());
    context->Display(handle, AIS_Shaded, 0, false);
}

void Viewport::Impl::hideDistanceHandle()
{
    removeHandle();
    handleDragging = false;
}

void Viewport::Impl::clearGizmoShapes()
{
    for (Handle(AIS_Shape)& shape : gizmoShapes) {
        if (!shape.IsNull()) {
            context->Remove(shape, false);
            shape.Nullify();
        }
    }
    gizmoBuiltSize = 0.0;
}

void Viewport::Impl::hideGizmo()
{
    clearGizmoShapes();
    gizmoKind = Viewport::GizmoKind::None;
    gizmoMask = 7;
    gizmoActive = -1;
    gizmoHiddenForZoom = false;
    if (gizmoSettle != nullptr) {
        gizmoSettle->stop();
    }
}

void Viewport::Impl::updateGizmoForZoom()
{
    if (gizmoKind == Viewport::GizmoKind::None || gizmoActive >= 0) {
        return;
    }
    const double pixel = pixelSize();
    const bool zooming = std::abs(pixel - gizmoLastPixel) > 1e-9 * std::max(pixel, 1e-12);
    gizmoLastPixel = pixel;
    if (gizmoBuilt() && std::abs(90.0 * pixel - gizmoBuiltSize) > 0.08 * gizmoBuiltSize) {
        clearGizmoShapes();
        gizmoHiddenForZoom = true;
    }
    if (!gizmoHiddenForZoom) {
        return;
    }
    if (gizmoSettle == nullptr) {
        gizmoSettle = new QTimer(&widget_);
        gizmoSettle->setSingleShot(true);
        QObject::connect(gizmoSettle, &QTimer::timeout, &widget_, [this] {
            if (gizmoHiddenForZoom && gizmoKind != Viewport::GizmoKind::None) {
                gizmoHiddenForZoom = false;
                showGizmo(gizmoKind, {gizmoOrigin.X(), gizmoOrigin.Y(), gizmoOrigin.Z()});
                widget_.update();
            }
        });
    }
    if (zooming || !gizmoSettle->isActive()) {
        gizmoSettle->start(900);  // otra vez a esperar mientras el zoom siga moviéndose
    }
}

void Viewport::Impl::showGizmo(Viewport::GizmoKind kind, const sketch::Vec3& origin)
{
    if (kind == Viewport::GizmoKind::None) {
        hideGizmo();
        return;
    }
    gizmoOrigin = toPnt(origin);
    const double size = 90.0 * pixelSize();
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(gp_Pnt(0.0, 0.0, 0.0), gizmoOrigin));
    const TopLoc_Location place(shift);
    // Mientras se arrastra solo cambia de lugar (rehacerlo cortaría el arrastre).
    if (gizmoBuilt() && gizmoKind == kind && (gizmoActive >= 0 || std::abs(size - gizmoBuiltSize) < 0.05 * gizmoBuiltSize)) {
        for (const Handle(AIS_Shape)& shape : gizmoShapes) {
            if (!shape.IsNull()) {
                context->SetLocation(shape, place);
            }
        }
        return;
    }
    clearGizmoShapes();
    gizmoKind = kind;
    gizmoHiddenForZoom = false;
    gizmoBuiltSize = size;
    const Quantity_Color colors[3] = {rgb(0xE5, 0x48, 0x4D), rgb(0x46, 0xC4, 0x6E), rgb(0x4C, 0x8D, 0xF6)};
    for (int k = 0; k < 3; ++k) {
        if (((gizmoMask >> k) & 1) == 0) {
            continue;
        }
        BRep_Builder builder;
        TopoDS_Compound shape;
        builder.MakeCompound(shape);
        const gp_Dir axis = gizmoAxis(k);
        if (kind == Viewport::GizmoKind::Move) {
            builder.Add(shape, BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(0, 0, 0), axis), 0.045 * size, 0.7 * size).Shape());
            builder.Add(shape, BRepPrimAPI_MakeCone(gp_Ax2(gp_Pnt(0, 0, 0).Translated(gp_Vec(axis) * (0.7 * size)), axis),
                                                    0.13 * size, 0.0, 0.3 * size).Shape());
        } else {
            const double radius = 0.85 * size;
            builder.Add(shape, BRepPrimAPI_MakeTorus(gp_Ax2(gp_Pnt(0, 0, 0), axis), radius, 0.026 * size).Shape());
            // Marcas en los ángulos conocidos (cada 45°; más largas en 0, 90, 180 y 270).
            const gp_Dir ref = gizmoRef(k);
            // La perilla del 0°: de aquí se miden los ángulos (y es donde está la marca larga de partida).
            builder.Add(shape, BRepPrimAPI_MakeSphere(gp_Pnt(0, 0, 0).Translated(gp_Vec(ref) * radius), 0.07 * size).Shape());
            for (int i = 0; i < 8; ++i) {
                const bool major = i % 2 == 0;
                gp_Vec radial(ref);
                radial.Rotate(gp_Ax1(gp_Pnt(0, 0, 0), axis), i * std::numbers::pi / 4.0);
                const double half = (major ? 0.13 : 0.085) * size;
                const gp_Pnt start = gp_Pnt(0, 0, 0).Translated(radial * (radius - half));
                builder.Add(shape, BRepPrimAPI_MakeCylinder(gp_Ax2(start, gp_Dir(radial)), (major ? 0.02 : 0.014) * size, 2.0 * half).Shape());
            }
        }
        Handle(AIS_Shape) ais = new AIS_Shape(shape);
        ais->SetColor(colors[k]);
        ais->Attributes()->SetFaceBoundaryDraw(false);
        // Formas pequeñas que se rehacen al cambiar el zoom: un mallado grueso (por defecto era muy fino, casi un
        // segundo para tres anillos con sus marcas) se ve igual de bien a este tamaño.
        ais->SetOwnDeviationCoefficient(0.05);
        ais->SetOwnDeviationAngle(0.6);
        ais->SetZLayer(Graphic3d_ZLayerId_Topmost);
        ais->SetLocalTransformation(shift);
        context->Display(ais, AIS_Shaded, 0, false);
        gizmoShapes[k] = ais;
    }
}

std::optional<gp_Pnt> Viewport::Impl::dragPlanePoint(const Vec2i& pixel) const
{
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Vec ray(dx, dy, dz);
    const gp_Vec normal(dragNormal);
    const double facing = ray.Dot(normal);
    if (ray.Magnitude() < 1e-12 || std::abs(facing / ray.Magnitude()) < 0.05) {
        return std::nullopt;
    }
    const double t = gp_Vec(gp_Pnt(x, y, z), dragGrab).Dot(normal) / facing;
    return gp_Pnt(x, y, z).Translated(ray * t);
}

std::optional<double> Viewport::Impl::gizmoLineParameter(const Vec2i& pixel, const gp_Pnt& origin, int k) const
{
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Vec ray(dx, dy, dz);
    if (ray.Magnitude() < 1e-12) {
        return std::nullopt;
    }
    const gp_Vec v = ray.Normalized();
    const gp_Vec d(gizmoAxis(k));
    const gp_Vec w(origin, gp_Pnt(x, y, z));
    const double b = v.Dot(d);
    const double denominator = 1.0 - b * b;
    if (denominator < 1e-6) {
        return std::nullopt;
    }
    return (w.Dot(d) - b * w.Dot(v)) / denominator;
}

std::optional<double> Viewport::Impl::gizmoRingAngle(const Vec2i& pixel, const gp_Pnt& origin, int k) const
{
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Vec ray(dx, dy, dz);
    const gp_Vec normal(gizmoAxis(k));
    const double facing = ray.Dot(normal);
    if (ray.Magnitude() < 1e-12 || std::abs(facing / ray.Magnitude()) < 0.05) {
        return std::nullopt;  // el plano se ve de canto
    }
    const double t = gp_Vec(gp_Pnt(x, y, z), origin).Dot(normal) / facing;
    const gp_Pnt hit = gp_Pnt(x, y, z).Translated(ray * t);
    const gp_Vec from(origin, hit);
    const gp_Vec ref(gizmoRef(k));
    return std::atan2(from.Dot(normal.Crossed(ref)), from.Dot(ref));
}

void Viewport::Impl::hideOtherGizmoShapes(int keep)
{
    for (int k = 0; k < 3; ++k) {
        if (k != keep && !gizmoShapes[k].IsNull() && context->IsDisplayed(gizmoShapes[k])) {
            context->Erase(gizmoShapes[k], false);
        }
    }
}

void Viewport::Impl::restoreGizmoShapes()
{
    for (const Handle(AIS_Shape)& shape : gizmoShapes) {
        if (!shape.IsNull() && !context->IsDisplayed(shape)) {
            context->Display(shape, AIS_Shaded, 0, false);
        }
    }
}

bool Viewport::Impl::startGizmoDrag(int k, const Vec2i& pixel)
{
    gizmoDragOrigin = gizmoOrigin;
    gizmoClock.restart();
    gizmoHasPending = false;
    if (gizmoKind == Viewport::GizmoKind::Move) {
        const auto t = gizmoLineParameter(pixel, gizmoDragOrigin, k);
        if (!t) {
            return false;
        }
        gizmoGrab = *t;
    } else {
        const auto a = gizmoRingAngle(pixel, gizmoDragOrigin, k);
        if (!a) {
            return false;
        }
        // El giro es el ángulo del cursor sobre el anillo medido desde su marca 0 (como en Orca Slicer), no lo
        // girado desde donde se agarró: así el cursor sobre una marca es exactamente ese ángulo.
        gizmoGrab = *a;
        gizmoTotal = *a;
    }
    gizmoActive = k;
    return true;
}

std::optional<double> Viewport::Impl::gizmoDragAmount(const Vec2i& pixel)
{
    if (gizmoKind == Viewport::GizmoKind::Move) {
        const auto t = gizmoLineParameter(pixel, gizmoDragOrigin, gizmoActive);
        return t ? std::optional<double>(*t - gizmoGrab) : std::nullopt;
    }
    const auto a = gizmoRingAngle(pixel, gizmoDragOrigin, gizmoActive);
    if (!a) {
        return std::nullopt;
    }
    // Ángulo absoluto del cursor, seguido de forma continua (la diferencia con el anterior envuelta a ±180°, para
    // poder dar más de una vuelta sin saltos).
    double delta = *a - gizmoGrab;
    while (delta > std::numbers::pi) {
        delta -= 2.0 * std::numbers::pi;
    }
    while (delta < -std::numbers::pi) {
        delta += 2.0 * std::numbers::pi;
    }
    gizmoTotal += delta;
    gizmoGrab = *a;
    return gizmoTotal;
}

void Viewport::Impl::hideGhost()
{
    if (!ghost.IsNull()) {
        context->Remove(ghost, false);
        ghost.Nullify();
    }
}

void Viewport::Impl::showGhost(const std::vector<geometry::Shape>& list, bool alert)
{
    hideGhost();
    BRep_Builder builder;
    TopoDS_Compound shape;
    builder.MakeCompound(shape);
    bool any = false;
    for (const geometry::Shape& body : list) {
        if (body.isNull()) {
            continue;
        }
        any = true;
        // Un cuerpo pesado (una rosca) sin malla, o con muchísimas caras (un engranaje), tardaría segundos en dibujarse aquí: su
        // sombra es su caja.
        if ((geometry::hasHeavySurface(body) && !geometry::hasMesh(body)) || body.faceCount() > 60) {
            const geometry::Box box = body.bounds();
            builder.Add(shape, BRepPrimAPI_MakeBox(gp_Pnt(box.min.x, box.min.y, box.min.z),
                                                   gp_Pnt(box.max.x, box.max.y, box.max.z)).Shape());
        } else {
            builder.Add(shape, geometry::occtShape(body));
        }
    }
    if (!any) {
        return;
    }
    ghost = new AIS_Shape(shape);
    ghost->SetColor(alert ? rgb(0xE5, 0x39, 0x35) : rgb(0x6C, 0xA6, 0xFF));
    ghost->SetTransparency(alert ? 0.15 : 0.55);
    // Es una sombra: un mallado grueso se ve igual de bien y no tarda segundos en un engranaje (los dientes).
    ghost->SetOwnDeviationCoefficient(0.01);
    ghost->SetOwnDeviationAngle(0.5);
    ghost->Attributes()->SetFaceBoundaryDraw(false);
    context->Display(ghost, AIS_Shaded, -1, false);
}

void Viewport::Impl::setGhostMotion(const sketch::Vec3& pivot, const sketch::Vec3& rotation, const sketch::Vec3& translation)
{
    if (ghost.IsNull()) {
        return;
    }
    // Lo mismo que hace moveBody: giros X, Y, Z por el pivote y después el desplazamiento.
    gp_Trsf total;
    const double around[3] = {rotation.x, rotation.y, rotation.z};
    for (int k = 0; k < 3; ++k) {
        if (std::abs(around[k]) > 1e-12) {
            gp_Trsf turn;
            turn.SetRotation(gp_Ax1(toPnt(pivot), gizmoAxis(k)), around[k]);
            total = turn * total;
        }
    }
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(translation.x, translation.y, translation.z));
    total = shift * total;
    context->SetLocation(ghost, TopLoc_Location(total));
}

void Viewport::Impl::hideAngleSweep()
{
    if (!sweepShape.IsNull()) {
        context->Remove(sweepShape, false);
        sweepShape.Nullify();
    }
}

void Viewport::Impl::showAngleSweep(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle)
{
    hideAngleSweep();
    const gp_Pnt c = toPnt(centerPoint);
    const gp_Vec radial(c, toPnt(startPoint));
    if (std::abs(angle) < 1e-4 || radial.Magnitude() < 1e-9) {
        return;
    }
    const gp_Circ circle(gp_Ax2(c, toDir(axisDir), gp_Dir(radial)), radial.Magnitude());
    const double a0 = std::min(0.0, angle);
    const double a1 = std::max(0.0, angle);
    const TopoDS_Edge arc = BRepBuilderAPI_MakeEdge(circle, a0, a1).Edge();
    sweepShape = new AIS_Shape(arc);
    sweepShape->SetColor(rgb(0x3F, 0xD0, 0xE0));
    sweepShape->Attributes()->WireAspect()->SetWidth(5.0);
    sweepShape->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(sweepShape, AIS_WireFrame, -1, false);
}

void Viewport::Impl::hideAlignmentLine()
{
    if (!alignShape.IsNull()) {
        context->Remove(alignShape, false);
        alignShape.Nullify();
    }
}

void Viewport::Impl::setEdgeAssist(bool on)
{
    edgeAssist = on;
    // 2 px es lo de fábrica; con la ayuda se agarra una arista desde unos píxeles más lejos (las aristas pesan más que las
    // caras cuando están a la misma profundidad, así que ganan a la cara de detrás).
    context->SetPixelTolerance(on ? 7 : 2);
    if (!on) {
        hideEdgeHover();
    }
}

void Viewport::Impl::hideEdgeHover()
{
    if (!hoverEdge.IsNull()) {
        context->Remove(hoverEdge, false);
        hoverEdge.Nullify();
        hoverEdgeShape.Nullify();
        view->Invalidate();
    }
}

bool Viewport::Impl::updateEdgeHover()
{
    if (!edgeAssist) {
        return false;
    }
    TopoDS_Shape edge;
    if (context->HasDetected()) {
        const Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(context->DetectedOwner());
        if (!owner.IsNull() && owner->HasShape() && owner->Shape().ShapeType() == TopAbs_EDGE
            && bodyOf(context->DetectedInteractive()) != nullptr) {
            edge = owner->Shape().Moved(owner->Location());
        }
    }
    if (edge.IsNull()) {
        const bool was = !hoverEdge.IsNull();
        hideEdgeHover();
        return was;
    }
    if (!hoverEdge.IsNull() && hoverEdgeShape.IsSame(edge)) {
        return false;  // ya está marcada
    }
    hideEdgeHover();
    hoverEdgeShape = edge;
    hoverEdge = new AIS_Shape(edge);
    hoverEdge->SetColor(rgb(0xF2, 0xA3, 0x4B));
    hoverEdge->Attributes()->WireAspect()->SetWidth(5.0);
    hoverEdge->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(hoverEdge, AIS_WireFrame, -1, false);  // sin modo de selección: solo se ve, no se agarra
    view->Invalidate();
    return true;
}

void Viewport::Impl::showAlignmentLine(const sketch::Vec3& from, const sketch::Vec3& to)
{
    hideAlignmentLine();
    const gp_Pnt a = toPnt(from);
    const gp_Pnt b = toPnt(to);
    if (a.Distance(b) < 1e-9) {
        return;
    }
    alignShape = new AIS_Shape(BRepBuilderAPI_MakeEdge(a, b).Edge());
    alignShape->SetColor(rgb(0xFF, 0xD8, 0x4C));
    alignShape->Attributes()->WireAspect()->SetWidth(4.0);
    alignShape->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(alignShape, AIS_WireFrame, -1, false);
}

void Viewport::Impl::showDimensionArrow(const sketch::Vec3& from, const sketch::Vec3& to)
{
    hideDimensionArrow();
    const gp_Pnt a = toPnt(from);
    const gp_Pnt b = toPnt(to);
    const double length = a.Distance(b);
    if (length < 1e-6) {
        return;
    }
    const gp_Dir dir(gp_Vec(a, b));
    // Puntas de tamaño fijo en pantalla (algo menores si la flecha es corta).
    const double head = std::min(18.0 * pixelSize(), 0.4 * length);
    const double radius = std::max(1.2 * pixelSize(), 1e-6);
    BRep_Builder builder;
    TopoDS_Compound arrow;
    builder.MakeCompound(arrow);
    const double shaft = std::max(length - 2.0 * head, 1e-6);
    builder.Add(arrow, BRepPrimAPI_MakeCylinder(gp_Ax2(a.Translated(gp_Vec(dir) * head), dir), radius, shaft).Shape());
    builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(a.Translated(gp_Vec(dir) * head), dir.Reversed()), head * 0.36, 0.0, head).Shape());
    builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(b.Translated(gp_Vec(dir) * -head), dir), head * 0.36, 0.0, head).Shape());
    dimArrow = new AIS_Shape(arrow);
    dimArrow->SetColor(rgb(0x3F, 0xD0, 0xE0));
    dimArrow->Attributes()->SetFaceBoundaryDraw(false);
    dimArrow->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(dimArrow, AIS_Shaded, -1, false);
}

void Viewport::Impl::showDimensionArc(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle)
{
    hideDimensionArrow();
    const gp_Pnt c = toPnt(centerPoint);
    const gp_Dir axis = toDir(axisDir);
    const gp_Pnt s0 = toPnt(startPoint);
    const gp_Vec radial(c, s0);
    if (std::abs(angle) < 1e-6 || radial.Magnitude() < 1e-9) {
        return;
    }
    const gp_Ax1 turn(c, axis);
    const double head = std::min(18.0 * pixelSize(), 0.25 * radial.Magnitude() * std::abs(angle));
    const double radius = std::max(1.2 * pixelSize(), 1e-6);
    const int steps = std::clamp(int(std::abs(angle) / (std::numbers::pi / 36.0)) + 1, 6, 72);
    std::vector<gp_Pnt> points;
    for (int i = 0; i <= steps; ++i) {
        gp_Pnt p = s0;
        p.Rotate(turn, angle * double(i) / double(steps));
        points.push_back(p);
    }
    BRep_Builder builder;
    TopoDS_Compound arrow;
    builder.MakeCompound(arrow);
    for (int i = 0; i < steps; ++i) {
        const gp_Vec v(points[std::size_t(i)], points[std::size_t(i) + 1]);
        if (v.Magnitude() > 1e-9) {
            builder.Add(arrow, BRepPrimAPI_MakeCylinder(gp_Ax2(points[std::size_t(i)], gp_Dir(v)), radius, v.Magnitude()).Shape());
        }
    }
    const gp_Vec first(points[0], points[1]);
    const gp_Vec last(points[std::size_t(steps) - 1], points[std::size_t(steps)]);
    if (first.Magnitude() > 1e-9 && last.Magnitude() > 1e-9) {
        const gp_Dir t0(first);
        const gp_Dir t1(last);
        builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(points[0].Translated(gp_Vec(t0) * head), t0.Reversed()), head * 0.36, 0.0, head).Shape());
        builder.Add(arrow, BRepPrimAPI_MakeCone(gp_Ax2(points[std::size_t(steps)].Translated(gp_Vec(t1) * -head), t1), head * 0.36, 0.0, head).Shape());
    }
    dimArrow = new AIS_Shape(arrow);
    dimArrow->SetColor(rgb(0x3F, 0xD0, 0xE0));
    dimArrow->Attributes()->SetFaceBoundaryDraw(false);
    dimArrow->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(dimArrow, AIS_Shaded, -1, false);
}

void Viewport::Impl::hideDimensionArrow()
{
    if (!dimArrow.IsNull()) {
        context->Remove(dimArrow, false);
        dimArrow.Nullify();
    }
}

void Viewport::Impl::removeHandle()
{
    if (!handle.IsNull()) {
        context->Remove(handle, false);
        handle.Nullify();
    }
}

std::optional<double> Viewport::Impl::handleParameterAt(const Vec2i& pixel) const
{
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_Vec ray(dx, dy, dz);
    if (ray.Magnitude() < 1e-12) {
        return std::nullopt;
    }
    const gp_Vec v = ray.Normalized();
    const gp_Vec d(handleDirection);
    const gp_Vec w(handleOrigin, gp_Pnt(x, y, z));
    const double b = v.Dot(d);
    const double denominator = 1.0 - b * b;
    if (denominator < 1e-6) {
        return std::nullopt;  // mirando a lo largo del eje
    }
    return (w.Dot(d) - b * w.Dot(v)) / denominator;
}

} // namespace nuf::render
