#include "ViewportImpl.hpp"

// Modo sketch: elegir plano, entrar y salir, punto bajo el cursor.
// Métodos de Viewport::Impl; el estado y las declaraciones están en ViewportImpl.hpp.

namespace nuf::render {

void Viewport::Impl::pickSketchPlane()
{
    if (mode != Mode::Model) {
        return;
    }
    createOriginPlanes();
    context->ClearSelected(false);
    for (const OriginPlane& origin : originPlanes) {
        context->Display(origin.shape, AIS_Shaded, 0, false);
        context->Activate(origin.shape, 0);
    }
    for (const ShownPlane& cp : constructionPlanes) {
        context->Activate(cp.shape, 0);
    }
    context->AddFilter(planeFilter);
    mode = Mode::PickPlane;
    activateBodies();
    emit widget_.selectionChanged(QString());
}

void Viewport::Impl::cancelPickSketchPlane()
{
    if (mode != Mode::PickPlane) {
        return;
    }
    context->ClearSelected(false);
    showOrigin();
    for (const ShownPlane& cp : constructionPlanes) {
        context->Deactivate(cp.shape);
    }
    context->RemoveFilter(planeFilter);
    mode = Mode::Model;
    activateBodies();
}

void Viewport::Impl::beginSketch(const sketch::Plane& plane)
{
    cancelPickSketchPlane();
    if (mode == Mode::Sketch) {
        endSketch();
    }
    context->ClearSelected(false);
    sketchPlane = plane;
    profilesPlaneDirty = true;
    grid->plane = toAx3(plane);
    updateGrid(true);
    context->Display(grid, 0, -1, false);
    context->Erase(ground, false);
    for (const Handle(SketchDrawing)& d : {drawing, preview}) {
        d->plane = toAx3(plane);
        d->clear();
        d->SetToUpdate();
        context->Display(d, 0, -1, false);
    }
    mode = Mode::Sketch;
    activateBodies();
    lookAtSketch();
    emit widget_.selectionChanged(QString());
}

void Viewport::Impl::endSketch()
{
    if (mode != Mode::Sketch) {
        return;
    }
    context->Erase(grid, false);
    context->Erase(drawing, false);
    context->Erase(preview, false);
    context->Erase(profiles, false);
    shownSketch.reset();
    projectMode = false;
    for (const Handle(AIS_InteractiveObject)& image : images) {
        context->Remove(image, false);
    }
    images.clear();
    mode = Mode::Model;
    activateBodies();
    updateGround(true);
    emit widget_.sketchCursorMoved(false, 0.0, 0.0);
}

void Viewport::Impl::lookAtSketch()
{
    if (mode != Mode::Sketch) {
        return;
    }
    const Handle(Graphic3d_Camera)& current = view->Camera();
    Handle(Graphic3d_Camera) start = new Graphic3d_Camera();
    start->Copy(current);
    Handle(Graphic3d_Camera) end = new Graphic3d_Camera();
    end->Copy(current);

    const sketch::Vec2 c = sketchPlane.toLocal(toVec3(current->Center().XYZ()));
    const gp_Pnt center = toPnt(sketchPlane.toWorld(c));
    const gp_Vec normal(toDir(sketchPlane.normal));
    end->SetEyeAndCenter(center.Translated(normal * current->Distance()), center);
    end->SetUp(toDir(sketchPlane.yAxis));

    const Handle(AIS_AnimationCamera)& animation = ViewAnimation();
    animation->Stop();
    animation->SetView(view);
    animation->SetCameraStart(start);
    animation->SetCameraEnd(end);
    animation->SetOwnDuration(0.4);
    animation->StartTimer(0.0, 1.0, true, false);
}

std::optional<sketch::Vec2> Viewport::Impl::sketchPointAt(const Vec2i& pixel) const
{
    double x = 0, y = 0, z = 0, dx = 0, dy = 0, dz = 0;
    view->ConvertWithProj(pixel.x(), pixel.y(), x, y, z, dx, dy, dz);
    const gp_XYZ from(x, y, z);
    const gp_XYZ dir(dx, dy, dz);
    const gp_XYZ normal = toDir(sketchPlane.normal).XYZ();
    const double facing = normal.Dot(dir);
    if (std::abs(facing) < 1e-9) {
        return std::nullopt;
    }
    const double t = normal.Dot(toPnt(sketchPlane.origin).XYZ() - from) / facing;
    return sketchPlane.toLocal(toVec3(from + dir * t));
}

void Viewport::Impl::showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
                const SketchHighlight& highlight)
{
    shownSketch = sketch;
    shownUnderconstrained = underconstrained;
    shownHighlight = highlight;
    rebuildSketch();

    // Regiones cerradas, sombreadas como en Fusion. Hallar los perfiles y
    // construir sus caras es lo más caro de esta función y no cambia al
    // mover el cursor ni al seleccionar: solo se rehace si cambió la
    // geometría (o el plano).
    const std::size_t fingerprint = sketch::geometryFingerprint(sketch);
    const bool geometryChanged = fingerprint != profilesFingerprint || !context->IsDisplayed(profiles)
                                 || profilesPlaneDirty;
    if (!geometryChanged) {
        return;
    }
    // Al arrastrar, la geometría cambia en cada movimiento del ratón y hallar los perfiles y construir sus caras es
    // lo más caro: se rehace como mucho cada 100 ms (y al soltar, con lo último).
    constexpr int kProfilesEveryMs = 100;
    if (context->IsDisplayed(profiles) && !profilesPlaneDirty && profilesClock.isValid() &&
        profilesClock.elapsed() < kProfilesEveryMs) {
        if (profilesTimer == nullptr) {
            profilesTimer = new QTimer(&widget_);
            profilesTimer->setSingleShot(true);
            QObject::connect(profilesTimer, &QTimer::timeout, &widget_, [this] {
                rebuildProfiles();
                view->Invalidate();
                widget_.update();
            });
        }
        profilesTimer->start(int(kProfilesEveryMs - profilesClock.elapsed()));
        return;
    }
    rebuildProfiles();
}

void Viewport::Impl::setProjectMode(bool on)
{
    projectMode = on;
    activateBodies();
    context->ClearDetected(false);
}

std::pair<std::vector<sketch::Geometry>, geometry::ElementRef> Viewport::Impl::projectionAt(const Vec2i& pixel)
{
    context->MoveTo(pixel.x(), pixel.y(), view, false);
    // Bajo el cursor puede haber varias aristas (por ejemplo, la costura
    // de un cilindro, que vista de frente se proyecta en un punto): se
    // usa la primera que da algo.
    for (context->InitDetected(); context->MoreDetected(); context->NextDetected()) {
        Handle(StdSelect_BRepOwner) owner = Handle(StdSelect_BRepOwner)::DownCast(context->DetectedCurrentOwner());
        if (owner.IsNull() || !owner->HasShape() || owner->Shape().ShapeType() != TopAbs_EDGE) {
            continue;
        }
        auto projected = projectEdge(TopoDS::Edge(owner->Shape()), sketchPlane);
        if (!projected.empty()) {
            geometry::ElementRef source;
            const Handle(AIS_InteractiveObject) object = Handle(AIS_InteractiveObject)::DownCast(owner->Selectable());
            if (const ShownBody* body = bodyOf(object)) {
                if (const auto index = geometry::edgeIndexOf(body->shape, owner->Shape())) {
                    source = body->shape.edgeRef(*index);
                }
            }
            return {projected, source};
        }
    }
    return {};
}

std::vector<sketch::Geometry> Viewport::Impl::textOutline(const QString& text, double height, const QString& family) const
{
    StdPrs_BRepFont font;
    const bool found = !family.isEmpty()
                       && font.FindAndInit(TCollection_AsciiString(family.toUtf8().constData()), Font_FA_Regular,
                                           height, Font_StrictLevel_Any);
    if (!found && !font.FindAndInit(pickLabelFont(), Font_FA_Regular, height)) {
        return {};
    }
    StdPrs_BRepTextBuilder builder;
    const TopoDS_Shape shape = builder.Perform(font, NCollection_String(text.toUtf8().constData()));
    // El texto se crea en el plano XY del modelo: sus coordenadas son ya
    // las del sketch. Muchas fuentes (las variables) dibujan las letras
    // con contornos que se solapan: se unen en un contorno limpio.
    return geometry::outlineOf(shape);
}

bool Viewport::Impl::showImage(const QString& path, sketch::Vec2 corner, double width, double height)
{
    const TopoDS_Face face =
        BRepBuilderAPI_MakeFace(gp_Pln(toAx3(sketchPlane)), corner.x, corner.x + width, corner.y, corner.y + height);
    // Se carga con Qt: el OCCT de las distribuciones a menudo no lee PNG/JPG.
    const QImage picture = QImage(path).convertToFormat(QImage::Format_RGBA8888);
    if (picture.isNull()) {
        return false;
    }
    Handle(Image_PixMap) pixels = new Image_PixMap();
    if (!pixels->InitTrash(Image_Format_RGBA, std::size_t(picture.width()), std::size_t(picture.height()))) {
        return false;
    }
    pixels->SetTopDown(true);
    for (int row = 0; row < picture.height(); ++row) {
        std::memcpy(pixels->ChangeRow(std::size_t(row)), picture.constScanLine(row), std::size_t(picture.width()) * 4);
    }
    Handle(AIS_TexturedShape) image = new AIS_TexturedShape(face);
    image->SetTexturePixMap(pixels);
    image->SetTextureMapOn();
    image->SetTextureRepeat(false, 1.0, 1.0);
    image->DisableTextureModulate();  // colores de la imagen, sin sombreado
    image->SetTransparency(0.25);
    image->Attributes()->SetFaceBoundaryDraw(false);
    context->Display(image, 3, -1, false);  // 3 = modo con textura
    images.push_back(image);
    return true;
}

std::vector<sketch::Geometry> Viewport::Impl::section() const
{
    std::vector<sketch::Geometry> result;
    BRepAlgoAPI_Section cut(allBodies(), gp_Pln(toAx3(sketchPlane)));
    if (!cut.IsDone()) {
        return result;
    }
    for (TopExp_Explorer it(cut.Shape(), TopAbs_EDGE); it.More(); it.Next()) {
        for (const sketch::Geometry& g : projectEdge(TopoDS::Edge(it.Current()), sketchPlane)) {
            result.push_back(g);
        }
    }
    return result;
}

void Viewport::Impl::showFinishedSketch(document::FeatureId id, const QString& name, const sketch::Sketch& sketch,
                        const sketch::Plane& plane)
{
    hideFinishedSketch(id);
    FinishedSketch shown;
    shown.name = name;
    const gp_Ax3 ax3 = toAx3(plane);
    const std::vector<sketch::Profile> found = sketch::findProfiles(sketch);
    for (std::size_t i = 0; i < found.size(); ++i) {
        const auto face = geometry::profileFace(sketch, found[i], ax3);
        if (!face) {
            continue;
        }
        Handle(AIS_Shape) region = new AIS_Shape(*face);
        region->SetColor(rgb(0x6C, 0xA6, 0xFF));
        region->SetTransparency(0.8);
        region->Attributes()->SetFaceBoundaryDraw(false);
        pullTowardCamera(region);
        region->SetZLayer(regionLayer);
        context->Display(region, AIS_Shaded, 0, false);
        shown.regions.emplace_back(region, int(i));
    }
    BRep_Builder builder;
    TopoDS_Compound edges;
    builder.MakeCompound(edges);
    for (const sketch::Entity& e : sketch.entities()) {
        if (!e.reference && !e.construction) {
            if (const auto edge = sketchEdge(e, ax3)) {
                builder.Add(edges, *edge);
                shown.curveEdges.emplace_back(*edge, e.id);
            }
        }
    }
    shown.curves = new AIS_Shape(edges);
    shown.curves->SetColor(rgb(0x8F, 0xB3, 0xE8));
    shown.curves->SetWidth(2.0);
    shown.curves->SetZLayer(Graphic3d_ZLayerId_Topmost);
    context->Display(shown.curves, AIS_WireFrame, AIS_Shape::SelectionMode(TopAbs_EDGE), false);
    finished[id] = shown;
}

void Viewport::Impl::hideFinishedSketch(document::FeatureId id)
{
    const auto it = finished.find(id);
    if (it == finished.end()) {
        return;
    }
    for (const auto& [region, index] : it->second.regions) {
        context->Remove(region, false);
    }
    context->Remove(it->second.curves, false);
    finished.erase(it);
}

std::optional<std::pair<document::FeatureId, bool>> Viewport::Impl::finishedOwner(const Handle(AIS_InteractiveObject)& object) const
{
    for (const auto& [id, shown] : finished) {
        if (object == shown.curves) {
            return std::pair{id, false};
        }
        for (const auto& [region, index] : shown.regions) {
            if (object == region) {
                return std::pair{id, true};
            }
        }
    }
    return std::nullopt;
}

} // namespace nuf::render
