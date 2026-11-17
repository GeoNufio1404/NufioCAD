#include "ViewportImpl.hpp"

// Cuerpos del modelo, mallado en segundo plano, sección, vista previa de herramientas y selección.
// Métodos de Viewport::Impl; el estado y las declaraciones están en ViewportImpl.hpp.

namespace nuf::render {

void Viewport::Impl::pullTowardCamera(const Handle(AIS_Shape)& shape)
{
    shape->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -2.0f, -4.0f);
}

void Viewport::Impl::applyTheme(bool dark)
{
    darkTheme = dark;
    grid->minorColor = dark ? rgb(0x41, 0x46, 0x4E) : rgb(0xCC, 0xD1, 0xD8);
    grid->majorColor = dark ? rgb(0x63, 0x6B, 0x76) : rgb(0x95, 0x9E, 0xAA);
    if (context->IsDisplayed(grid)) {
        context->Redisplay(grid, false);
    }
    ground->minorColor = dark ? rgb(0x3A, 0x3F, 0x47) : rgb(0xD5, 0xD9, 0xDF);
    ground->majorColor = dark ? rgb(0x52, 0x59, 0x63) : rgb(0xAE, 0xB5, 0xBF);
    updateGround(true);
    if (dark) {
        view->SetBgGradientColors(rgb(0x3A, 0x3F, 0x47), rgb(0x1E, 0x21, 0x26),
                                  Aspect_GradientFillMethod_Vertical, false);
    } else {
        view->SetBgGradientColors(rgb(0xF7, 0xF8, 0xFA), rgb(0xC9, 0xCF, 0xD6),
                                  Aspect_GradientFillMethod_Vertical, false);
    }
    viewCube->SetTextColor(dark ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29));
    viewCube->SetBoxColor(dark ? rgb(0x4A, 0x50, 0x59) : rgb(0xE6, 0xE9, 0xED));
    context->Redisplay(viewCube, false);
    // El sketch que se está mostrando lleva los colores del tema baked en su dibujo (trazos, medidas, iconos):
    // hay que rehacerlo, o se quedan los del tema anterior hasta el siguiente cambio.
    if (mode == Mode::Sketch && shownSketch) {
        rebuildSketch();
    }
    view->Invalidate();
}

void Viewport::Impl::describeSelection()
{
    int faces = 0;
    int edges = 0;
    int regions = 0;
    int curves = 0;
    QStringList sketchNames;
    for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
        // Lo de un sketch terminado: perfiles (regiones) y curvas.
        if (const auto owner = finishedOwner(context->SelectedInteractive())) {
            ++(owner->second ? regions : curves);
            const QString& name = finished.at(owner->first).name;
            if (!sketchNames.contains(name)) {
                sketchNames << name;
            }
            continue;
        }
        if (!context->HasSelectedShape()) {
            continue;
        }
        switch (context->SelectedShape().ShapeType()) {
        case TopAbs_FACE: ++faces; break;
        case TopAbs_EDGE: ++edges; break;
        default: break;
        }
    }

    QStringList parts;
    if (faces == 1) {
        parts << QStringLiteral("1 cara");
    } else if (faces > 1) {
        parts << QStringLiteral("%1 caras").arg(faces);
    }
    if (edges == 1) {
        parts << QStringLiteral("1 arista");
    } else if (edges > 1) {
        parts << QStringLiteral("%1 aristas").arg(edges);
    }
    if (regions + curves > 0) {
        QStringList what;
        if (regions > 0) {
            what << (regions == 1 ? QStringLiteral("1 perfil") : QStringLiteral("%1 perfiles").arg(regions));
        }
        if (curves > 0) {
            what << (curves == 1 ? QStringLiteral("1 curva") : QStringLiteral("%1 curvas").arg(curves));
        }
        parts << QStringLiteral("%1 de %2").arg(what.join(QStringLiteral(" y ")), sketchNames.join(QStringLiteral(", ")));
    }
    emit widget_.selectionChanged(parts.isEmpty() ? QString()
                                                  : QStringLiteral("Seleccionado: ") + parts.join(QStringLiteral(", ")));
    emit widget_.modelSelectionChanged();
}

void Viewport::Impl::setBusy(bool on)
{
    if (busy != on) {
        busy = on;
        emit widget_.busyChanged(on);
    }
}

void Viewport::Impl::setWaiting(bool waiting)
{
    if (waiting == waitingForMesh) {
        return;
    }
    waitingForMesh = waiting;
    const std::uint64_t token = ++busyToken;
    if (!waiting) {
        setBusy(false);
        return;
    }
    QTimer::singleShot(150, &widget_, [this, token] {
        if (waitingForMesh && token == busyToken) {
            setBusy(true);
        }
    });
}

Viewport::Impl::BodyMeshing Viewport::Impl::meshingFor(const geometry::Shape& shape) const
{
    if (geometry::hasHeavySurface(shape)) {
        return {geometry::kHeavyDeviation, geometry::kHeavyAngle};
    }
    const Handle(Prs3d_Drawer)& drawer = context->DefaultDrawer();
    return {StdPrs_ToolTriangulatedShape::GetDeflection(geometry::occtShape(shape), drawer), drawer->DeviationAngle()};
}

bool Viewport::Impl::needsMesh(const geometry::Shape& shape) const
{
    return !shape.isNull() && !geometry::hasMesh(shape) &&
           std::ranges::find(unmeshable, static_cast<const void*>(shape.impl())) == unmeshable.end();
}

void Viewport::Impl::startMeshTask(const geometry::Shape& shape)
{
    auto task = std::make_unique<MeshTask>();
    task->id = nextMeshTaskId++;
    task->shape = shape;
    MeshTask* raw = task.get();
    const std::uint64_t id = task->id;
    Impl* self = this;
    const BodyMeshing meshing = meshingFor(shape);  // en el hilo de la interfaz: mira el tamaño de la pieza
    task->task = geometry::TaskPool::instance().submit([raw, self, id, meshing] {
        raw->mesh = geometry::prepareMesh(raw->shape, meshing.deviation, meshing.angle, geometry::currentCancelFlag());
        if (geometry::cancelRequested()) {
            return;
        }
        QMetaObject::invokeMethod(&self->widget_, [self, id] { self->meshTaskFinished(id); }, Qt::QueuedConnection);
    });
    meshTasks.push_back(std::move(task));
}

void Viewport::Impl::meshTaskFinished(std::uint64_t id)
{
    const auto it = std::ranges::find_if(meshTasks, [&](const auto& t) { return t->id == id; });
    if (it == meshTasks.end()) {
        return;  // cancelada y retirada
    }
    (*it)->task->wait();  // ya terminó (avisó al acabar)
    if (!(*it)->task->cancelled()) {
        if ((*it)->mesh.valid()) {
            geometry::applyMesh((*it)->shape, (*it)->mesh);
        } else {
            if (unmeshable.size() > 64) {
                unmeshable.clear();  // son solo direcciones: no se guardan para siempre
            }
            unmeshable.push_back((*it)->shape.impl());
        }
    }
    meshTasks.erase(it);
    const std::vector<document::Body> again = wantedBodies;
    showBodies(again);
    showPendingTool();
    if (fitWhenReady && !waitingForMesh) {
        fitWhenReady = false;
        widget_.fitAll();
    }
    view->Invalidate();
    widget_.update();
}

void Viewport::Impl::reapCancelled()
{
    std::erase_if(cancelledTasks, [](const std::unique_ptr<MeshTask>& t) { return t->task->finished(); });
}

void Viewport::Impl::cancelMeshTasks()
{
    for (auto* list : {&meshTasks, &cancelledTasks}) {
        for (auto& t : *list) {
            t->task->cancel();
        }
    }
    for (auto* list : {&meshTasks, &cancelledTasks}) {
        for (auto& t : *list) {
            t->task->wait();
        }
        list->clear();
    }
}

void Viewport::Impl::showBodies(const std::vector<document::Body>& list)
{
    wantedBodies = list;
    // ¿Hay cuerpos pesados sin malla? Se preparan en segundo plano y, hasta que acaben, se deja lo que había.
    bool waiting = false;
    for (const document::Body& body : list) {
        const bool shown = std::ranges::any_of(bodies, [&](const ShownBody& b) {
            return b.key == body.key && b.shape.impl() == body.shape.impl();
        });
        if (shown || !needsMesh(body.shape)) {
            continue;
        }
        waiting = true;
        if (std::ranges::none_of(meshTasks, [&](const auto& t) { return t->shape.impl() == body.shape.impl(); })) {
            startMeshTask(body.shape);
        }
    }
    // Lo que ya no se pide se cancela (el hilo se recoge cuando termine, o al cerrar).
    for (auto it = meshTasks.begin(); it != meshTasks.end();) {
        const bool wanted = std::ranges::any_of(list, [&](const document::Body& body) {
            return body.shape.impl() == (*it)->shape.impl();
        }) || (!pendingTool.isNull() && pendingTool.impl() == (*it)->shape.impl());
        if (!wanted) {
            (*it)->task->cancel();
            cancelledTasks.push_back(std::move(*it));
            it = meshTasks.erase(it);
        } else {
            ++it;
        }
    }
    reapCancelled();
    setWaiting(waiting);
    if (waiting) {
        return;
    }
    std::vector<ShownBody> next;
    std::vector<Handle(AIS_Shape)> created;
    for (const document::Body& body : list) {
        const auto old = std::ranges::find_if(bodies, [&](const ShownBody& b) {
            return b.key == body.key && b.shape.impl() == body.shape.impl();
        });
        if (old != bodies.end()) {
            next.push_back(*old);
            bodies.erase(old);
            continue;
        }
        Handle(AIS_Shape) ais = new AIS_Shape(geometry::occtShape(body.shape));
        ais->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Plastified));
        ais->SetColor(rgb(0xB7, 0xBE, 0xC7));
        if (body.key.rfind("gen:", 0) == 0) {
            // La vista previa de un generador (todavía no está en el diseño): azul translúcido, como la de lo que
            // una herramienta quita o añade, para no confundirla con lo que ya existe.
            ais->SetColor(rgb(0x6C, 0xA6, 0xFF));
            ais->SetTransparency(0.45);
        }
        if (geometry::hasHeavySurface(body.shape) || geometry::hasMesh(body.shape)) {
            // Mallar es lo que más paraba la ventana (una rosca con el mallado normal daba cientos de miles de
            // triángulos y minutos de espera; un engranaje, más de un segundo): el cuerpo se malla en segundo plano
            // (ver startMeshTask) con una desviación absoluta que da lo mismo que daría el visor, y aquí ya no se
            // vuelve a mallar.
            const BodyMeshing meshing = meshingFor(body.shape);
            ais->Attributes()->SetTypeOfDeflection(Aspect_TOD_ABSOLUTE);
            ais->Attributes()->SetMaximalChordialDeviation(meshing.deviation);
            ais->SetOwnDeviationAngle(meshing.angle);
            if (geometry::hasMesh(body.shape)) {
                ais->Attributes()->SetAutoTriangulation(false);
            }
        }
        // El modo de selección va en Display: con -1 el cuerpo queda como
        // no seleccionable y un Activate posterior no lo revierte.
        if (!clipSection.IsNull()) {
            ais->AddClipPlane(clipSection);
        }
        context->Display(ais, AIS_Shaded, AIS_Shape::SelectionMode(TopAbs_FACE), false);
        next.push_back({body.key, body.shape, ais});
        created.push_back(ais);
    }
    for (const ShownBody& gone : bodies) {
        context->Remove(gone.ais, false);
    }
    bodies = std::move(next);
    // Los cuerpos que ya se mostraban conservan su selección (al cambiar
    // de modo se reactivan todos con activateBodies()); solo se activan
    // los nuevos. En un arrastre con vista previa hay uno nuevo por paso.
    for (const ShownBody& b : bodies) {
        if (std::ranges::find(created, b.ais) != created.end()) {
            activateBody(b);
        }
    }
}

void Viewport::Impl::setSection(const sketch::Vec3& origin, const sketch::Vec3& normal)
{
    const bool fresh = clipSection.IsNull();
    if (fresh) {
        clipSection = new Graphic3d_ClipPlane();
        clipSection->SetCapping(true);
        clipSection->SetCappingColor(rgb(0x6C, 0xA6, 0xFF));
    }
    clipSection->SetEquation(gp_Pln(toPnt(origin), toDir(normal)));
    if (fresh) {
        for (const ShownBody& b : bodies) {
            b.ais->AddClipPlane(clipSection);
        }
    }
}

void Viewport::Impl::clearSection()
{
    if (clipSection.IsNull()) {
        return;
    }
    for (const ShownBody& b : bodies) {
        b.ais->RemoveClipPlane(clipSection);
    }
    clipSection.Nullify();
}

void Viewport::Impl::activateBody(const ShownBody& b)
{
    context->Deactivate(b.ais);
    switch (mode) {
    case Mode::Model:
        context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_FACE));
        context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_EDGE));
        break;
    case Mode::PickPlane:
        context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_FACE));
        break;
    case Mode::Sketch:
        if (projectMode) {
            context->Activate(b.ais, AIS_Shape::SelectionMode(TopAbs_EDGE));
        }
        break;
    }
}

void Viewport::Impl::activateBodies()
{
    for (const ShownBody& b : bodies) {
        activateBody(b);
    }
}

const Viewport::Impl::ShownBody* Viewport::Impl::bodyOf(const Handle(AIS_InteractiveObject)& object) const
{
    const auto it = std::ranges::find_if(bodies, [&](const ShownBody& b) { return b.ais == object; });
    return it == bodies.end() ? nullptr : &*it;
}

TopoDS_Shape Viewport::Impl::allBodies() const
{
    BRep_Builder builder;
    TopoDS_Compound all;
    builder.MakeCompound(all);
    for (const ShownBody& b : bodies) {
        builder.Add(all, geometry::occtShape(b.shape));
    }
    return all;
}

void Viewport::Impl::showToolPreview(const geometry::Shape& tool, bool removes)
{
    clearToolPreview();
    if (tool.isNull()) {
        return;
    }
    pendingTool = tool;
    pendingToolRemoves = removes;
    if (needsMesh(tool)) {
        if (std::ranges::none_of(meshTasks, [&](const auto& t) { return t->shape.impl() == tool.impl(); })) {
            startMeshTask(tool);
        }
        return;
    }
    showPendingTool();
}

void Viewport::Impl::showPendingTool()
{
    if (pendingTool.isNull() || !toolPreview.IsNull() || needsMesh(pendingTool)) {
        return;
    }
    toolPreview = new AIS_Shape(geometry::occtShape(pendingTool));
    toolPreview->SetColor(pendingToolRemoves ? rgb(0xE5, 0x48, 0x4D) : rgb(0x6C, 0xA6, 0xFF));
    toolPreview->SetTransparency(0.55);
    toolPreview->Attributes()->SetFaceBoundaryDraw(false);
    if (geometry::hasMesh(pendingTool)) {
        const BodyMeshing meshing = meshingFor(pendingTool);
        toolPreview->Attributes()->SetTypeOfDeflection(Aspect_TOD_ABSOLUTE);
        toolPreview->Attributes()->SetMaximalChordialDeviation(meshing.deviation);
        toolPreview->SetOwnDeviationAngle(meshing.angle);
        toolPreview->Attributes()->SetAutoTriangulation(false);
    }
    context->Display(toolPreview, AIS_Shaded, -1, false);
}

void Viewport::Impl::showToolEdges(const geometry::Shape& body, const std::vector<int>& edges)
{
    if (!toolEdges.IsNull()) {
        context->Remove(toolEdges, false);
        toolEdges.Nullify();
    }
    if (body.isNull() || edges.empty()) {
        return;
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (int index : edges) {
        builder.Add(compound, geometry::edgeAt(body, index));
    }
    toolEdges = new AIS_Shape(compound);
    toolEdges->SetColor(rgb(0x2F, 0x80, 0xED));
    toolEdges->SetWidth(3.5);
    context->Display(toolEdges, AIS_WireFrame, -1, false);
}

void Viewport::Impl::showToolFaces(const geometry::Shape& body, const std::vector<int>& faces)
{
    if (!toolFaces.IsNull()) {
        context->Remove(toolFaces, false);
        toolFaces.Nullify();
    }
    if (body.isNull() || faces.empty()) {
        return;
    }
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (int index : faces) {
        builder.Add(compound, geometry::faceAt(body, index));
    }
    toolFaces = new AIS_Shape(compound);
    toolFaces->SetColor(rgb(0x2F, 0x80, 0xED));
    toolFaces->SetTransparency(0.45);
    toolFaces->Attributes()->SetFaceBoundaryDraw(false);
    context->Display(toolFaces, AIS_Shaded, -1, false);
}

void Viewport::Impl::clearToolPreview()
{
    pendingTool = geometry::Shape();
    if (!toolFaces.IsNull()) {
        context->Remove(toolFaces, false);
        toolFaces.Nullify();
    }
    if (!toolEdges.IsNull()) {
        context->Remove(toolEdges, false);
        toolEdges.Nullify();
    }
    if (!toolPreview.IsNull()) {
        context->Remove(toolPreview, false);
        toolPreview.Nullify();
    }
}

ModelSelection Viewport::Impl::modelSelection() const
{
    ModelSelection result;
    for (context->InitSelected(); context->MoreSelected(); context->NextSelected()) {
        const Handle(AIS_InteractiveObject) object = context->SelectedInteractive();
        for (const auto& [id, shown] : finished) {
            for (std::size_t r = 0; r < shown.regions.size(); ++r) {
                if (shown.regions[r].first == object) {
                    result.profiles.push_back({id, shown.regions[r].second});
                }
            }
            if (object == shown.curves && context->HasSelectedShape()) {
                for (const auto& [edge, entity] : shown.curveEdges) {
                    if (edge.IsSame(context->SelectedShape())) {
                        result.curves.push_back({id, entity});
                    }
                }
            }
        }
        const ShownBody* body = bodyOf(object);
        if (body == nullptr || !context->HasSelectedShape()) {
            continue;
        }
        const TopoDS_Shape& picked = context->SelectedShape();
        if (picked.ShapeType() == TopAbs_FACE) {
            if (const auto index = geometry::faceIndexOf(body->shape, picked)) {
                result.faces.push_back({body->key, body->shape.faceRef(*index)});
            }
        } else if (picked.ShapeType() == TopAbs_EDGE) {
            if (const auto index = geometry::edgeIndexOf(body->shape, picked)) {
                result.edges.push_back({body->key, body->shape.edgeRef(*index)});
            }
        }
    }
    return result;
}

void Viewport::Impl::setAdditiveSelection(bool on)
{
    // Clic sin modificadores: alterna (como Shift) o sustituye.
    ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton,
                                       on ? AIS_SelectionScheme_XOR : defaultClickScheme);
}

} // namespace nuf::render
