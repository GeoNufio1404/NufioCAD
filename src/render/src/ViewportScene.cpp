#include "ViewportImpl.hpp"

// Escena: navegación, cubo de vistas, planos y ejes de construcción, origen, suelo y rejilla.
// Métodos de Viewport::Impl; el estado y las declaraciones están en ViewportImpl.hpp.

namespace nuf::render {

void Viewport::Impl::handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                      const Handle(V3d_View)& v)
{
    // La cámara ya está actualizada para este cuadro: se ajusta la rejilla
    // antes de dibujar.
    if (mode != Mode::Sketch) {
        updateGround(false);
    }
    if (mode == Mode::PickPlane || (originShown && mode == Mode::Model)) {
        fitOriginPlanes(false);
    }
    if (mode == Mode::Sketch) {
        updateGrid(false);
        if (shownSketch && std::abs(pixelSize() - annotationScale) > 0.01 * annotationScale) {
            rebuildSketch();
        }
    }
    updateGizmoForZoom();
    AIS_ViewController::handleViewRedraw(ctx, v);
    // Con la ayuda de aristas, ya con lo que hay bajo el cursor decidido en este cuadro, se marca (o se desmarca) la arista;
    // el cambio se ve en el cuadro siguiente.
    if (edgeAssist && updateEdgeHover()) {
        widget_.update();
    }
    if (toAskNextFrame()) {
        widget_.update();
    }
}

void Viewport::Impl::OnSelectionChanged(const Handle(AIS_InteractiveContext)& /*ctx*/,
                        const Handle(V3d_View)& /*v*/)
{
    // El punto donde cayó el clic (si lo hay bajo el cursor): lo usan el agujero y la rosca puestos con un clic en una cara.
    lastPick.reset();
    if (!context.IsNull() && context->HasDetected() && context->MainSelector()->NbPicked() > 0) {
        lastPick = context->MainSelector()->PickedPoint(1);
    }
    if (mode == Mode::PickPlane) {
        pickedSketchPlane();
    } else {
        describeSelection();
    }
}

void Viewport::Impl::setNavigationMode(Viewport::NavigationMode newMode)
{
    navMode = newMode;
    configureNavigation();
}

void Viewport::Impl::configureNavigation()
{
    // Izquierdo: clic selecciona y arrastre orbita. Shift + izquierdo: clic
    // agrega/quita de la selección y arrastre selecciona por rectángulo.
    // Derecho: arrastre desplaza; el clic queda libre para el menú rápido.
    // Botón central: también desplaza; Shift + central orbita (Fusion 360).
    // Botón + modificadores, como clave del mapa (enums distintos en OCCT).
    const auto key = [](unsigned int button, unsigned int modifiers = 0) { return button | modifiers; };
    auto& gestures = ChangeMouseGestureMap();
    gestures.Clear();
    // Con una herramienta de la barra de navegación, el arrastre izquierdo
    // hace solo eso (orbitar, desplazar o zoom).
    AIS_MouseGesture leftDrag = AIS_MouseGesture_RotateOrbit;
    if (navMode == Viewport::NavigationMode::Pan) {
        leftDrag = AIS_MouseGesture_Pan;
    } else if (navMode == Viewport::NavigationMode::Zoom) {
#if OCC_VERSION_HEX >= 0x070700
        leftDrag = AIS_MouseGesture_ZoomVertical;
#else
        leftDrag = AIS_MouseGesture_Zoom; // OCCT < 7.7 no distingue el zoom vertical
#endif
    }
    gestures.Bind(key(Aspect_VKeyMouse_LeftButton), leftDrag);
    gestures.Bind(key(Aspect_VKeyMouse_LeftButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_SelectRectangle);
    gestures.Bind(key(Aspect_VKeyMouse_RightButton), AIS_MouseGesture_Pan);
    gestures.Bind(key(Aspect_VKeyMouse_MiddleButton), AIS_MouseGesture_Pan);
    gestures.Bind(key(Aspect_VKeyMouse_MiddleButton, Aspect_VKeyFlags_SHIFT), AIS_MouseGesture_RotateOrbit);
    MouseSelectionSchemes().Find(Aspect_VKeyMouse_LeftButton, defaultClickScheme);
}

void Viewport::Impl::configureHighlight()
{
    const Handle(Prs3d_Drawer)& drawer = context->DefaultDrawer();
    drawer->SetFaceBoundaryDraw(true);
    drawer->SetFaceBoundaryAspect(new Prs3d_LineAspect(rgb(0x1F, 0x23, 0x29), Aspect_TOL_SOLID, 1.0));

    // Resaltado con el azul de la interfaz: claro al pasar, intenso al
    // seleccionar. Sombreado para rellenar la cara (no solo su contorno).
    for (auto type : {Prs3d_TypeOfHighlight_Dynamic, Prs3d_TypeOfHighlight_LocalDynamic}) {
        context->HighlightStyle(type)->SetColor(rgb(0x6C, 0xA6, 0xFF));
        context->HighlightStyle(type)->SetDisplayMode(AIS_Shaded);
        // En la capa del propio objeto (OCCT lo pone en Top, encima de
        // todo): así lo que está delante, como un cilindro, lo tapa.
        context->HighlightStyle(type)->SetZLayer(Graphic3d_ZLayerId_UNKNOWN);
    }
    for (auto type : {Prs3d_TypeOfHighlight_Selected, Prs3d_TypeOfHighlight_LocalSelected}) {
        context->HighlightStyle(type)->SetColor(rgb(0x1F, 0x6F, 0xEB));
        context->HighlightStyle(type)->SetDisplayMode(AIS_Shaded);
    }
}

void Viewport::Impl::createViewCube()
{
    viewCube = new AIS_ViewCube();
    viewCube->SetViewAnimation(ViewAnimation());
    viewCube->SetFixedAnimationLoop(false);
    viewCube->SetAutoStartAnimation(true);
    viewCube->SetFitSelected(false); // encuadra toda la pieza, no la selección
    // El resaltado del cubo usa su aspecto de sombreado (cian por defecto).
    const Handle(Prs3d_Drawer)& cubeHover = viewCube->DynamicHilightAttributes();
    if (!cubeHover.IsNull() && cubeHover->HasOwnShadingAspect()) {
        cubeHover->ShadingAspect()->SetColor(rgb(0x6C, 0xA6, 0xFF));
    }
    viewCube->SetSize(55);
    const TCollection_AsciiString font = pickLabelFont();
    viewCube->SetFont(font);
    viewCube->SetFontHeight(10);
    // Letras de los ejes con la misma fuente y los colores habituales en CAD.
    // El ViewCube las toma del dibujante por defecto del contexto.
    context->DefaultDrawer()->TextAspect()->Aspect()->SetFont(font);
    const Handle(Prs3d_DatumAspect)& datum = context->DefaultDrawer()->DatumAspect();
    const std::pair<Prs3d_DatumParts, Quantity_Color> axes[] = {
        {Prs3d_DatumParts_XAxis, rgb(0xE5, 0x48, 0x4D)},
        {Prs3d_DatumParts_YAxis, rgb(0x2F, 0x9E, 0x44)},
        {Prs3d_DatumParts_ZAxis, rgb(0x3B, 0x82, 0xF6)}};
    for (const auto& [datumPart, color] : axes) {
        datum->TextAspect(datumPart)->Aspect()->SetFont(font);
        datum->TextAspect(datumPart)->SetColor(color);
    }
    viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER, Vec2i(85, 85)));
    // Eje Z hacia arriba, como en los slicers de impresión 3D.
    viewCube->SetBoxSideLabel(V3d_Xpos, "DERECHA");
    viewCube->SetBoxSideLabel(V3d_Xneg, "IZQUIERDA");
    viewCube->SetBoxSideLabel(V3d_Ypos, "ATRÁS");
    viewCube->SetBoxSideLabel(V3d_Yneg, "FRENTE");
    viewCube->SetBoxSideLabel(V3d_Zpos, "ARRIBA");
    viewCube->SetBoxSideLabel(V3d_Zneg, "ABAJO");
}

double Viewport::Impl::referenceHalf() const
{
    double largest = 0.0;
    for (const ShownBody& b : bodies) {
        const geometry::Box box = b.shape.bounds();
        if (!box.empty) {
            largest = std::max({largest, box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z});
        }
    }
    return std::max(30.0, 0.75 * largest);
}

void Viewport::Impl::showConstructionAxes(const std::vector<Viewport::ConstructionAxisView>& wanted)
{
    const double half = referenceHalf() * 1.5;
    const auto same = [&](const ShownAxis& a, const Viewport::ConstructionAxisView& b) {
        const auto eq = [](const sketch::Vec3& p, const sketch::Vec3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
        return a.id == b.id && a.half == half && eq(a.point, b.point) && eq(a.direction, b.direction);
    };
    if (wanted.size() == constructionAxes.size()
        && std::ranges::all_of(std::views::iota(std::size_t(0), wanted.size()),
                               [&](std::size_t i) { return same(constructionAxes[i], wanted[i]); })) {
        return;
    }
    for (const ShownAxis& old : constructionAxes) {
        context->Remove(old.shape, false);
    }
    constructionAxes.clear();
    for (const Viewport::ConstructionAxisView& item : wanted) {
        const gp_Pnt a(item.point.x - item.direction.x * half, item.point.y - item.direction.y * half,
                       item.point.z - item.direction.z * half);
        const gp_Pnt b(item.point.x + item.direction.x * half, item.point.y + item.direction.y * half,
                       item.point.z + item.direction.z * half);
        Handle(AIS_Shape) shape = new AIS_Shape(BRepBuilderAPI_MakeEdge(a, b).Edge());
        shape->SetColor(rgb(0x39, 0xB5, 0xC8));
        shape->SetWidth(2.5);
        shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(shape, 0, -1, false);
        constructionAxes.push_back({item.id, item.point, item.direction, half, shape});
    }
}

void Viewport::Impl::showConstructionPlanes(const std::vector<Viewport::ConstructionPlaneView>& wanted)
{
    const double half = referenceHalf();
    const auto same = [&](const ShownPlane& a, const Viewport::ConstructionPlaneView& b) {
        const auto eq = [](const sketch::Vec3& p, const sketch::Vec3& q) { return p.x == q.x && p.y == q.y && p.z == q.z; };
        return a.id == b.id && a.name == b.name && a.half == half && eq(a.plane.origin, b.plane.origin)
               && eq(a.plane.xAxis, b.plane.xAxis) && eq(a.plane.yAxis, b.plane.yAxis);
    };
    if (wanted.size() == constructionPlanes.size()
        && std::ranges::all_of(std::views::iota(std::size_t(0), wanted.size()),
                               [&](std::size_t i) { return same(constructionPlanes[i], wanted[i]); })) {
        return;
    }
    for (const ShownPlane& old : constructionPlanes) {
        context->Remove(old.shape, false);
    }
    constructionPlanes.clear();
    for (const Viewport::ConstructionPlaneView& item : wanted) {
        Handle(AIS_Shape) shape = new AIS_Shape(
            TopoDS_Shape(BRepBuilderAPI_MakeFace(gp_Pln(toAx3(item.plane)), -half, half, -half, half)));
        shape->SetColor(rgb(0x39, 0xB5, 0xC8));
        shape->SetTransparency(0.75);
        shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
        context->Display(shape, AIS_Shaded, 0, false);
        if (mode != Mode::PickPlane) {
            context->Deactivate(shape);
        }
        constructionPlanes.push_back({item.id, item.name, item.plane, half, shape});
    }
}

void Viewport::Impl::showOrigin()
{
    if (originShown && originPlanes.empty()) {
        createOriginPlanes();
    }
    for (const OriginPlane& origin : originPlanes) {
        if (originShown) {
            context->Display(origin.shape, AIS_Shaded, 0, false);
            context->Deactivate(origin.shape);
        } else {
            context->Erase(origin.shape, false);
        }
    }
}

void Viewport::Impl::createOriginPlanes()
{
    for (const OriginPlane& origin : originPlanes) {
        context->Remove(origin.shape, false);
    }
    originPlanes.clear();
    const std::tuple<sketch::Plane, QString, document::OriginPlane> planes[] = {
        {sketch::Plane::xy(), QStringLiteral("Plano XY"), document::OriginPlane::XY},
        {sketch::Plane::xz(), QStringLiteral("Plano XZ"), document::OriginPlane::XZ},
        {sketch::Plane::yz(), QStringLiteral("Plano YZ"), document::OriginPlane::YZ}};
    for (const auto& [plane, name, kind] : planes) {
        Handle(AIS_Shape) shape = new AIS_Shape(TopoDS_Shape());
        shape->SetColor(rgb(0xF2, 0xA3, 0x4B));
        shape->SetTransparency(0.6);
        // Por encima de la pieza: suelen atravesarla y quedarían ocultos.
        shape->SetZLayer(Graphic3d_ZLayerId_Topmost);
        originPlanes.push_back({shape, plane, name, kind});
    }
    originSize = 0.0;
    fitOriginPlanes(true);
}

void Viewport::Impl::fitOriginPlanes(bool force)
{
    if (originPlanes.empty()) {
        return;
    }
    double width = 0, height = 0;
    view->Size(width, height);
    const double size = 0.25 * std::max(1e-3, std::min(width, height));
    // Hacia la cámara: contrario a la dirección de la vista.
    const gp_Dir toEye = view->Camera()->Direction().Reversed();
    const std::array<double, 3> signs{toEye.X() >= 0.0 ? 1.0 : -1.0, toEye.Y() >= 0.0 ? 1.0 : -1.0,
                                      toEye.Z() >= 0.0 ? 1.0 : -1.0};
    if (!force && signs == originSigns && originSize > 0.0 && std::abs(size - originSize) < 0.15 * originSize) {
        return;
    }
    originSigns = signs;
    originSize = size;
    for (const OriginPlane& origin : originPlanes) {
        // Signo de cada eje del plano (sus ejes son ejes del modelo).
        const auto sign = [&](const sketch::Vec3& axis) {
            return axis.x != 0.0 ? signs[0] : axis.y != 0.0 ? signs[1] : signs[2];
        };
        const double su = sign(origin.plane.xAxis) * size;
        const double sv = sign(origin.plane.yAxis) * size;
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(gp_Pln(toAx3(origin.plane)), std::min(0.0, su),
                                                         std::max(0.0, su), std::min(0.0, sv), std::max(0.0, sv));
        origin.shape->SetShape(face);
        if (context->IsDisplayed(origin.shape)) {
            context->Redisplay(origin.shape, false, true);
            context->RecomputeSelectionOnly(origin.shape);
        } else {
            origin.shape->SetToUpdate();
        }
    }
}

void Viewport::Impl::pickedSketchPlane()
{
    context->InitSelected();
    if (!context->MoreSelected()) {
        return;
    }
    std::optional<std::pair<sketch::Plane, QString>> picked;
    document::SketchAttachment attachment;
    const Handle(AIS_InteractiveObject) object = context->SelectedInteractive();
    for (const OriginPlane& origin : originPlanes) {
        if (origin.shape == object) {
            picked.emplace(origin.plane, origin.name);
            attachment.origin = origin.kind;
        }
    }
    for (const ShownPlane& cp : constructionPlanes) {
        if (cp.shape == object) {
            picked.emplace(cp.plane, cp.name);
            attachment.origin.reset();
            attachment.reference = cp.id;
        }
    }
    if (!picked && context->HasSelectedShape() && context->SelectedShape().ShapeType() == TopAbs_FACE) {
        const TopoDS_Face face = TopoDS::Face(context->SelectedShape());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane) {
            const gp_Pln pln = surface.Plane();
            gp_Dir normal = pln.Axis().Direction();
            if (face.Orientation() == TopAbs_REVERSED) {
                normal.Reverse();
            }
            picked.emplace(sketch::Plane::fromPointAndNormal(toVec3(pln.Location().XYZ()), toVec3(normal.XYZ())),
                           QStringLiteral("Cara plana"));
            attachment.origin.reset();
            if (const ShownBody* body = bodyOf(object)) {
                if (const auto index = geometry::faceIndexOf(body->shape, face)) {
                    attachment.face = body->shape.faceRef(*index);
                }
            }
        }
    }
    context->ClearSelected(false);
    if (!picked) {
        return;
    }
    attachment.lastPlane = picked->first;
    // Se avisa fuera del procesado de eventos de OCCT: quien recibe la
    // señal cambia el contexto (beginSketch).
    QMetaObject::invokeMethod(
        &widget_, [widget = &widget_, plane = picked->first, name = picked->second, attachment] {
            emit widget->sketchPlanePicked(plane, name, attachment);
        },
        Qt::QueuedConnection);
}

void Viewport::Impl::updateGround(bool force)
{
    if (mode == Mode::Sketch || view->Window().IsNull()) {
        return;
    }
    if (!groundShown) {
        context->Erase(ground, false);
        return;
    }
    const bool changed = fitGrid(*ground, sketch::Plane::xy(), force);
    if (!context->IsDisplayed(ground)) {
        ground->SetToUpdate();
        context->Display(ground, 0, -1, false);
    } else if (changed) {
        context->Redisplay(ground, false);
    }
}

bool Viewport::Impl::fitGrid(SketchGrid& target, const sketch::Plane& plane, bool force) const
{
    double width = 0, height = 0;
    view->Size(width, height);
    const double size = std::max({width, height, 1e-3});
    constexpr double kMinSpacingPx = 7.0;
    const double step = std::pow(10.0, std::ceil(std::log10(kMinSpacingPx * pixelSize())));
    const double block = 10.0 * step;
    const sketch::Vec2 c = plane.toLocal(toVec3(view->Camera()->Center().XYZ()));
    const double cx = std::round(c.x / block) * block;
    const double cy = std::round(c.y / block) * block;
    const double half = std::ceil(1.5 * size / block) * block;
    if (!force && step == target.step && cx == target.centerX && cy == target.centerY && half == target.half) {
        return false;
    }
    target.step = step;
    target.centerX = cx;
    target.centerY = cy;
    target.half = half;
    return true;
}

void Viewport::Impl::updateGrid(bool force)
{
    if (!fitGrid(*grid, sketchPlane, force)) {
        return;
    }
    if (context->IsDisplayed(grid)) {
        context->Redisplay(grid, false);
    } else {
        grid->SetToUpdate(); // si no, Display reutiliza la presentación vieja
    }
}

} // namespace nuf::render
