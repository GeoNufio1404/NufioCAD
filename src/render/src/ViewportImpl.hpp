#pragma once

// El Impl del visor (estado y manejo de OCCT). Solo lo incluye Viewport*.cpp.

#include "ViewportDrawing.hpp"

namespace nuf::render {

class Viewport::Impl final : public AIS_ViewController {
public:
    explicit Impl(Viewport& widget)
        : widget_(widget)
    {
        Handle(Aspect_DisplayConnection) display = new Aspect_DisplayConnection();
        Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(display, false);
        // QOpenGLWidget hace el intercambio de buffers y compone la ventana.
        driver->ChangeOptions().buffersNoSwap = true;
        driver->ChangeOptions().buffersOpaqueAlpha = true;
        driver->ChangeOptions().useSystemBuffer = false;
        driver->ChangeOptions().ffpEnable = !kCoreProfile;

        viewer = new V3d_Viewer(driver);
        viewer->SetDefaultLights();
        viewer->SetLightOn();

        context = new AIS_InteractiveContext(viewer);
        configureHighlight();

        view = viewer->CreateView();
        view->SetImmediateUpdate(false);

        configureNavigation();
        createViewCube();

        planeFilter = new SketchPlaneFilter();
        grid = new SketchGrid();
        // Rejilla del suelo (plano XY): referencia de dónde está «abajo»
        // fuera del sketch, como en Fusion 360.
        ground = new SketchGrid();
        ground->plane = gp_Ax3(gp::XOY());
        // Las rejillas no cuentan al encuadrar (Ajustar a la ventana).
        grid->SetInfiniteState(true);
        ground->SetInfiniteState(true);
        // Capa propia para las regiones (perfiles) de los sketches: respeta la
        // profundidad de la pieza (lo que está delante la tapa) y, por ser una
        // capa superior, al seleccionar gana a la pieza. Para que sobre una
        // cara en el mismo plano no se peleen, cada región se acerca a la
        // cámara con su propio desplazamiento (pullTowardCamera).
        Graphic3d_ZLayerSettings regionSettings;
        regionSettings.SetName("Regiones de sketch");
        regionSettings.SetClearDepth(false);
        regionSettings.SetEnableDepthTest(true);
        regionSettings.SetEnableDepthWrite(true);
        viewer->InsertLayerBefore(regionLayer, regionSettings, Graphic3d_ZLayerId_Topmost);
        // La geometría del sketch se ve aunque la pieza la tape (por ejemplo,
        // un sketch en el plano XY bajo la pieza).
        drawing = new SketchDrawing();
        drawing->SetZLayer(Graphic3d_ZLayerId_Topmost);
        drawing->font = pickLabelFont();
        preview = new SketchDrawing();
        preview->SetZLayer(Graphic3d_ZLayerId_Topmost);
        profiles = new AIS_Shape(TopoDS_Shape());
        profiles->SetZLayer(regionLayer);
        profiles->SetColor(rgb(0x6C, 0xA6, 0xFF));
        profiles->SetTransparency(0.82);
        profiles->Attributes()->SetFaceBoundaryDraw(false);
        pullTowardCamera(profiles);
    }

    // Las caras de la pieza ya se alejan un poco (factor 1); esto acerca la
    // región aunque esté inclinada respecto a la vista.
    static void pullTowardCamera(const Handle(AIS_Shape)& shape)
    {
        shape->Attributes()->ShadingAspect()->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, -2.0f, -4.0f);
    }

    enum class Mode { Model, PickPlane, Sketch };

    void applyTheme(bool dark)
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
        view->Invalidate();
    }

    void describeSelection()
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

    // --- Cuerpos del modelo ------------------------------------------------

    struct ShownBody {
        std::string key;
        geometry::Shape shape;
        Handle(AIS_Shape) ais;
    };

    // --- Mallado en segundo plano ---------------------------------------------
    // Los cuerpos pesados (una rosca) tardan segundos en mallarse y en calcular sus normales: se hace en
    // un hilo de trabajo sobre una copia, y mientras tanto se sigue viendo lo anterior.

    struct MeshTask {
        std::uint64_t id = 0;
        geometry::Shape shape;
        std::atomic<bool> cancel{false};
        std::atomic<bool> finished{false};
        geometry::PreparedMesh mesh;
        std::thread thread;
    };
    std::vector<std::unique_ptr<MeshTask>> meshTasks;
    std::vector<std::unique_ptr<MeshTask>> cancelledTasks;  // canceladas que aún no han terminado
    std::vector<document::Body> wantedBodies;               // la última lista pedida
    std::uint64_t nextMeshTaskId = 1;
    std::vector<const void*> unmeshable;                    // su malla en segundo plano falló: se dibujan como siempre
    bool busy = false;

    void setBusy(bool on)
    {
        if (busy != on) {
            busy = on;
            emit widget_.busyChanged(on);
        }
    }

    void startMeshTask(const geometry::Shape& shape)
    {
        auto task = std::make_unique<MeshTask>();
        task->id = nextMeshTaskId++;
        task->shape = shape;
        MeshTask* raw = task.get();
        const std::uint64_t id = task->id;
        Impl* self = this;
        task->thread = std::thread([raw, self, id] {
            raw->mesh = geometry::prepareMesh(raw->shape, geometry::kHeavyDeviation, geometry::kHeavyAngle, raw->cancel);
            raw->finished = true;
            QMetaObject::invokeMethod(&self->widget_, [self, id] { self->meshTaskFinished(id); }, Qt::QueuedConnection);
        });
        meshTasks.push_back(std::move(task));
    }

    // En el hilo de la interfaz: la malla ya está; se pasa al cuerpo y se vuelve a pintar.
    void meshTaskFinished(std::uint64_t id)
    {
        const auto it = std::ranges::find_if(meshTasks, [&](const auto& t) { return t->id == id; });
        if (it == meshTasks.end()) {
            return;  // cancelada y retirada
        }
        (*it)->thread.join();
        if (!(*it)->cancel.load()) {
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
        view->Invalidate();
        widget_.update();
    }

    void reapCancelled()
    {
        std::erase_if(cancelledTasks, [](const std::unique_ptr<MeshTask>& t) {
            if (!t->finished.load()) {
                return false;
            }
            if (t->thread.joinable()) {
                t->thread.join();
            }
            return true;
        });
    }

    void cancelMeshTasks()
    {
        for (auto* list : {&meshTasks, &cancelledTasks}) {
            for (auto& t : *list) {
                t->cancel = true;
            }
        }
        for (auto* list : {&meshTasks, &cancelledTasks}) {
            for (auto& t : *list) {
                if (t->thread.joinable()) {
                    t->thread.join();
                }
            }
            list->clear();
        }
    }

    void showBodies(const std::vector<document::Body>& list)
    {
        wantedBodies = list;
        // ¿Hay cuerpos pesados sin malla? Se preparan en segundo plano y, hasta que acaben, se deja lo que había.
        bool waiting = false;
        for (const document::Body& body : list) {
            const bool shown = std::ranges::any_of(bodies, [&](const ShownBody& b) {
                return b.key == body.key && b.shape.impl() == body.shape.impl();
            });
            if (shown || !geometry::hasHeavySurface(body.shape) || geometry::hasMesh(body.shape) ||
                std::ranges::find(unmeshable, static_cast<const void*>(body.shape.impl())) != unmeshable.end()) {
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
            });
            if (!wanted) {
                (*it)->cancel = true;
                cancelledTasks.push_back(std::move(*it));
                it = meshTasks.erase(it);
            } else {
                ++it;
            }
        }
        reapCancelled();
        setBusy(waiting);
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
            if (geometry::hasHeavySurface(body.shape)) {
                // Una superficie spline con miles de puntos de control (una rosca) con el mallado normal
                // (relativo, 0,001 del tamaño) daba cientos de miles de triángulos y minutos de espera:
                // se malla (en segundo plano) con una desviación absoluta de 0,1 mm, que se ve lisa con el
                // sombreado, y aquí ya no se vuelve a mallar.
                ais->Attributes()->SetTypeOfDeflection(Aspect_TOD_ABSOLUTE);
                ais->Attributes()->SetMaximalChordialDeviation(geometry::kHeavyDeviation);
                ais->SetOwnDeviationAngle(geometry::kHeavyAngle);
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

    // --- Sección -------------------------------------------------------------

    Handle(Graphic3d_ClipPlane) clipSection;

    void setSection(const sketch::Vec3& origin, const sketch::Vec3& normal)
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

    void clearSection()
    {
        if (clipSection.IsNull()) {
            return;
        }
        for (const ShownBody& b : bodies) {
            b.ais->RemoveClipPlane(clipSection);
        }
        clipSection.Nullify();
    }

    // Modos de selección de un cuerpo según lo que se esté haciendo.
    void activateBody(const ShownBody& b)
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

    void activateBodies()
    {
        for (const ShownBody& b : bodies) {
            activateBody(b);
        }
    }

    const ShownBody* bodyOf(const Handle(AIS_InteractiveObject)& object) const
    {
        const auto it = std::ranges::find_if(bodies, [&](const ShownBody& b) { return b.ais == object; });
        return it == bodies.end() ? nullptr : &*it;
    }

    // Todos los cuerpos en una forma (para cortar con el plano, medir…).
    TopoDS_Shape allBodies() const
    {
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const ShownBody& b : bodies) {
            builder.Add(all, geometry::occtShape(b.shape));
        }
        return all;
    }

    void showToolPreview(const geometry::Shape& tool, bool removes)
    {
        clearToolPreview();
        if (tool.isNull()) {
            return;
        }
        toolPreview = new AIS_Shape(geometry::occtShape(tool));
        toolPreview->SetColor(removes ? rgb(0xE5, 0x48, 0x4D) : rgb(0x6C, 0xA6, 0xFF));
        toolPreview->SetTransparency(0.55);
        toolPreview->Attributes()->SetFaceBoundaryDraw(false);
        context->Display(toolPreview, AIS_Shaded, -1, false);
    }

    void showToolEdges(const geometry::Shape& body, const std::vector<int>& edges)
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

    void showToolFaces(const geometry::Shape& body, const std::vector<int>& faces)
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

    void clearToolPreview()
    {
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

    ModelSelection modelSelection() const
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

    void setAdditiveSelection(bool on)
    {
        // Clic sin modificadores: alterna (como Shift) o sustituye.
        ChangeMouseSelectionSchemes().Bind(Aspect_VKeyMouse_LeftButton,
                                           on ? AIS_SelectionScheme_XOR : defaultClickScheme);
    }

    // --- Flecha de distancia -------------------------------------------------

    void showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance)
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

    void hideDistanceHandle()
    {
        removeHandle();
        handleDragging = false;
    }

    // --- Mover y girar con el ratón ---------------------------------------------

    static gp_Dir gizmoAxis(int k) { return k == 0 ? gp_Dir(1, 0, 0) : k == 1 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1); }
    // De dónde se mide el ángulo de cada anillo: X de Y hacia Z, Y de Z hacia X, Z de X hacia Y.
    static gp_Dir gizmoRef(int k) { return k == 0 ? gp_Dir(0, 1, 0) : k == 1 ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0); }

    Viewport::GizmoKind gizmoKind = Viewport::GizmoKind::None;
    Handle(AIS_Shape) gizmoShapes[3];
    gp_Pnt gizmoOrigin;
    double gizmoBuiltSize = 0.0;
    int gizmoActive = -1;  // el eje que se está arrastrando
    gp_Pnt gizmoDragOrigin;  // dónde estaba el centro al agarrar (el arrastre se mide desde ahí)
    double gizmoGrab = 0.0;  // mover: parámetro sobre el eje al agarrar; girar: ángulo anterior
    double gizmoTotal = 0.0;
    bool gizmoHiddenForZoom = false;
    double gizmoLastPixel = 0.0;
    QTimer* gizmoSettle = nullptr;
    Handle(AIS_Shape) ghost;

    void clearGizmoShapes()
    {
        for (Handle(AIS_Shape)& shape : gizmoShapes) {
            if (!shape.IsNull()) {
                context->Remove(shape, false);
                shape.Nullify();
            }
        }
        gizmoBuiltSize = 0.0;
    }

    void hideGizmo()
    {
        clearGizmoShapes();
        gizmoKind = Viewport::GizmoKind::None;
        gizmoActive = -1;
        gizmoHiddenForZoom = false;
        if (gizmoSettle != nullptr) {
            gizmoSettle->stop();
        }
    }

    // Las flechas y los anillos guardan su tamaño en pantalla, pero rehacerlos a cada cuadro de un zoom (decenas de
    // sólidos) deja la vista a 1 imagen por segundo: al cambiar el zoom se quitan y se vuelven a dibujar cuando la
    // vista lleva un momento quieta.
    void updateGizmoForZoom()
    {
        if (gizmoKind == Viewport::GizmoKind::None || gizmoActive >= 0) {
            return;
        }
        const double pixel = pixelSize();
        const bool zooming = std::abs(pixel - gizmoLastPixel) > 1e-9 * std::max(pixel, 1e-12);
        gizmoLastPixel = pixel;
        if (!gizmoShapes[0].IsNull() && std::abs(90.0 * pixel - gizmoBuiltSize) > 0.08 * gizmoBuiltSize) {
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

    void showGizmo(Viewport::GizmoKind kind, const sketch::Vec3& origin)
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
        if (!gizmoShapes[0].IsNull() && gizmoKind == kind && (gizmoActive >= 0 || std::abs(size - gizmoBuiltSize) < 0.05 * gizmoBuiltSize)) {
            for (const Handle(AIS_Shape)& shape : gizmoShapes) {
                context->SetLocation(shape, place);
            }
            return;
        }
        clearGizmoShapes();
        gizmoKind = kind;
        gizmoHiddenForZoom = false;
        gizmoBuiltSize = size;
        const Quantity_Color colors[3] = {rgb(0xE5, 0x48, 0x4D), rgb(0x46, 0xC4, 0x6E), rgb(0x4C, 0x8D, 0xF6)};
        for (int k = 0; k < 3; ++k) {
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

    // Parámetro sobre el eje «k» que pasa por «origin» del punto más cercano al rayo del píxel.
    std::optional<double> gizmoLineParameter(const Vec2i& pixel, const gp_Pnt& origin, int k) const
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

    // Ángulo del punto del plano del anillo «k» al que apunta el píxel, medido desde la marca 0 del anillo.
    std::optional<double> gizmoRingAngle(const Vec2i& pixel, const gp_Pnt& origin, int k) const
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

    // Mientras se arrastra una flecha o un anillo, los otros dos se quitan (la vista queda limpia para ver cómo quedaría);
    // al soltar vuelven. Quitarlos de la vista no los destruye: se vuelven a mostrar tal cual.
    void hideOtherGizmoShapes(int keep)
    {
        for (int k = 0; k < 3; ++k) {
            if (k != keep && !gizmoShapes[k].IsNull() && context->IsDisplayed(gizmoShapes[k])) {
                context->Erase(gizmoShapes[k], false);
            }
        }
    }

    void restoreGizmoShapes()
    {
        for (const Handle(AIS_Shape)& shape : gizmoShapes) {
            if (!shape.IsNull() && !context->IsDisplayed(shape)) {
                context->Display(shape, AIS_Shaded, 0, false);
            }
        }
    }

    bool startGizmoDrag(int k, const Vec2i& pixel)
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

    // Lo arrastrado hasta ahora (mm o rad), o nada si el ratón apunta a un lugar sin respuesta (plano de canto).
    std::optional<double> gizmoDragAmount(const Vec2i& pixel)
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

    void hideGhost()
    {
        if (!ghost.IsNull()) {
            context->Remove(ghost, false);
            ghost.Nullify();
        }
    }

    void showGhost(const std::vector<geometry::Shape>& list)
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
            // Un cuerpo pesado (una rosca) sin malla tardaría segundos en dibujarse aquí: su sombra es su caja.
            if (geometry::hasHeavySurface(body) && !geometry::hasMesh(body)) {
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
        ghost->SetColor(rgb(0x6C, 0xA6, 0xFF));
        ghost->SetTransparency(0.55);
        ghost->Attributes()->SetFaceBoundaryDraw(false);
        context->Display(ghost, AIS_Shaded, -1, false);
    }

    void setGhostMotion(const sketch::Vec3& pivot, const sketch::Vec3& rotation, const sketch::Vec3& translation)
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

    // El arco de lo que se lleva girado en un arrastre: una sola arista gruesa desde «start» hasta «angle» (rad), alrededor
    // del eje que pasa por «center». Se rehace a cada paso del ratón, así que tiene que ser barata (el arco con
    // cilindros y puntas del dimensionamiento son decenas de sólidos).
    Handle(AIS_Shape) sweepShape;

    void hideAngleSweep()
    {
        if (!sweepShape.IsNull()) {
            context->Remove(sweepShape, false);
            sweepShape.Nullify();
        }
    }

    void showAngleSweep(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle)
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

    Handle(AIS_Shape) alignShape;

    void hideAlignmentLine()
    {
        if (!alignShape.IsNull()) {
            context->Remove(alignShape, false);
            alignShape.Nullify();
        }
    }

    // La guía de alineación: una recta entre dos puntos (a través del centro del anillo, en el plano del anillo).
    void showAlignmentLine(const sketch::Vec3& from, const sketch::Vec3& to)
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

    // Los movimientos del ratón de un arrastre llegan más deprisa de lo que se dibuja: se agrupan (el último vale).
    QTimer* gizmoFlush = nullptr;
    QElapsedTimer gizmoClock;
    double gizmoPending = 0.0;
    bool gizmoHasPending = false;

    // --- Flecha de medida (doble punta) -----------------------------------------

    void showDimensionArrow(const sketch::Vec3& from, const sketch::Vec3& to)
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

    void showDimensionArc(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle)
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

    void hideDimensionArrow()
    {
        if (!dimArrow.IsNull()) {
            context->Remove(dimArrow, false);
            dimArrow.Nullify();
        }
    }

    void removeHandle()
    {
        if (!handle.IsNull()) {
            context->Remove(handle, false);
            handle.Nullify();
        }
    }

    // Parámetro sobre el eje de la flecha del punto más cercano al rayo del píxel.
    std::optional<double> handleParameterAt(const Vec2i& pixel) const
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

    // --- Modo sketch -------------------------------------------------------

    void pickSketchPlane()
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

    void cancelPickSketchPlane()
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

    void beginSketch(const sketch::Plane& plane)
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

    void endSketch()
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

    // Anima la cámara hasta mirar el plano de frente, con el eje Y del sketch
    // hacia arriba, sin cambiar el zoom ni alejarse del punto que se miraba.
    void lookAtSketch()
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

    // Punto del plano del sketch bajo el píxel, si el rayo lo corta.
    std::optional<sketch::Vec2> sketchPointAt(const Vec2i& pixel) const
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

    // Colores de la geometría del sketch (como en Fusion: azul = aún libre).
    SketchStroke freeStroke() const { return {darkTheme ? rgb(0x6C, 0xA6, 0xFF) : rgb(0x1F, 0x6F, 0xEB)}; }
    SketchStroke fixedStroke() const { return {darkTheme ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29)}; }
    SketchStroke constructionStroke() const { return {rgb(0xF2, 0xA3, 0x4B), 1.5, Aspect_TOL_DASH}; }

    static Quantity_Color selectedColor() { return rgb(0xFF, 0xAA, 0x33); }
    static Quantity_Color hoveredColor() { return rgb(0xFF, 0xD5, 0x8A); }

    void showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
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
        if (geometryChanged) {
            profilesFingerprint = fingerprint;
            profilesPlaneDirty = false;
            profiles->SetShape(profileCompound(sketch, toAx3(sketchPlane)));
            profiles->SetToUpdate();
            if (context->IsDisplayed(profiles)) {
                context->Redisplay(profiles, false);
            } else {
                context->Display(profiles, AIS_Shaded, -1, false);
            }
        }
    }

    // Medida de 12.50 mm como «12.5» (sin ceros de sobra).
    static std::string formatMeasure(double value)
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

    // Largo de cada línea, Ø de cada círculo y R de cada arco, para tener
    // referencia del tamaño sin tener que acotar. Lo que ya tiene una cota
    // propia no se repite, y lo proyectado (cientos de curvas) ni la
    // construcción se rotulan.
    void addMeasures(const sketch::Sketch& sketch, const std::set<sketch::EntityId>& focus,
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

    // Vuelve a dibujar el último sketch mostrado (también al cambiar el zoom,
    // porque las anotaciones se colocan en píxeles).
    void rebuildSketch()
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
        const Quantity_Color dimmed = darkTheme ? rgb(0x9A, 0xA3, 0xAE) : rgb(0x6B, 0x74, 0x80);
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
        for (const sketch::Annotation& note : sketch::layoutAnnotations(sketch, annotationScale, &layoutMemory)) {
            const bool picked =
                std::ranges::find(highlight.selectedConstraints, note.constraint) != highlight.selectedConstraints.end();
            const bool hovered = note.constraint == highlight.hoveredConstraint;
            bool relevant = note.dimension || constraintsAll || picked || hovered;
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
            drawing->addText(note.anchor, note.text, color, box, note.dimension ? 13.0 : 11.0);
        }
        if (measuresVisible) {
            addMeasures(sketch, focus, dimmed, text);
        }
        context->Redisplay(drawing, false);
    }

    void showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint,
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
            preview->addText(badgeAt, badge, rgb(0xF2, 0xA3, 0x4B), rgb(0x1F, 0x23, 0x29), 16.0);
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

    double pixelSize() const
    {
        double width = 0, height = 0;
        view->Size(width, height);
        int pixelsWide = 0, pixelsHigh = 0;
        view->Window()->Size(pixelsWide, pixelsHigh);
        return width / std::max(pixelsWide, 1);
    }

    double gridStep() const { return grid->step; }

    void setProjectMode(bool on)
    {
        projectMode = on;
        activateBodies();
        context->ClearDetected(false);
    }

    // Arista de la pieza bajo el píxel, proyectada, y su referencia.
    std::pair<std::vector<sketch::Geometry>, geometry::ElementRef> projectionAt(const Vec2i& pixel)
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

    std::vector<sketch::Geometry> textOutline(const QString& text, double height, const QString& family) const
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

    bool showImage(const QString& path, sketch::Vec2 corner, double width, double height)
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

    std::vector<sketch::Geometry> section() const
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

    // Sketch terminado en la vista 3D: una forma por región (perfil),
    // seleccionable entera, y una con todas las curvas, seleccionables por
    // arista. Van por encima de la pieza: un sketch suele quedar dentro o
    // debajo de ella, y lo que se dibuja ahí debe poder elegirse.
    struct FinishedSketch {
        QString name;
        std::vector<std::pair<Handle(AIS_Shape), int>> regions;  // y su índice de perfil
        Handle(AIS_Shape) curves;
        std::vector<std::pair<TopoDS_Edge, sketch::EntityId>> curveEdges;
    };

    void showFinishedSketch(document::FeatureId id, const QString& name, const sketch::Sketch& sketch,
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

    void hideFinishedSketch(document::FeatureId id)
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

    // Sketch terminado al que pertenece un objeto, y si es una región.
    std::optional<std::pair<document::FeatureId, bool>> finishedOwner(const Handle(AIS_InteractiveObject)& object) const
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

    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_ViewCube) viewCube;
    Mode mode = Mode::Model;
    bool sketchToolActive = false;
    bool projectMode = false;
    bool sketchDragging = false;
    std::optional<sketch::Sketch> shownSketch;
    std::size_t profilesFingerprint = 0;  // geometría con la que se calcularon los perfiles sombreados
    bool profilesPlaneDirty = true;        // el plano cambió: hay que rehacerlos aunque la geometría sea la misma
    std::vector<sketch::EntityId> shownUnderconstrained;
    SketchHighlight shownHighlight;
    double annotationScale = 0.0;
    sketch::LayoutMemory layoutMemory;  // nivel elegido por cota entre repintados
    bool measuresVisible = true;
    bool measuresAll = false;      // todas las medidas, no solo las de lo que se mira
    bool constraintsAll = false;   // todas las restricciones, no solo las de lo que se mira
    bool leftPressed = false;
    Viewport::NavigationMode navMode = Viewport::NavigationMode::Default;
    std::map<document::FeatureId, FinishedSketch> finished;
    std::vector<ShownBody> bodies;
    bool originShown = false;
    bool groundShown = true;
    double originSize = 0.0;
    std::array<double, 3> originSigns{1.0, 1.0, 1.0};
    Handle(AIS_Shape) handle;  // flecha de distancia
    Handle(AIS_Shape) dimArrow;  // flecha de doble punta de la medida en edición
    double handleDistance = 0.0;
    bool handleDragging = false;
    double handleGrab = 0.0;  // parámetro del eje donde se agarró, menos la distancia
    bool ignoreNextRelease = false;
    QPointF pressPosition;
    std::optional<sketch::Vec2> pressSketchPoint;  // dónde se pulsó, en el plano

protected:
    // Pide otro cuadro mientras haya animaciones (ViewCube, inercia).
    void handleViewRedraw(const Handle(AIS_InteractiveContext)& ctx,
                          const Handle(V3d_View)& v) override
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
        if (toAskNextFrame()) {
            widget_.update();
        }
    }

    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& /*ctx*/,
                            const Handle(V3d_View)& /*v*/) override
    {
        if (mode == Mode::PickPlane) {
            pickedSketchPlane();
        } else {
            describeSelection();
        }
    }

public:
    void setNavigationMode(Viewport::NavigationMode newMode)
    {
        navMode = newMode;
        configureNavigation();
    }

private:
    void configureNavigation()
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

    void configureHighlight()
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

    void createViewCube()
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

    struct ShownPlane {
        document::FeatureId id = 0;
        QString name;
        sketch::Plane plane;
        double half = 0.0;
        Handle(AIS_Shape) shape;
    };

    // Los planos de construcción son cuadrados centrados en el origen del plano;
    // su tamaño sigue al de las piezas (como mínimo 60 mm de lado).
public:
    double referenceHalf() const
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

    struct ShownAxis {
        document::FeatureId id = 0;
        sketch::Vec3 point;
        sketch::Vec3 direction;
        double half = 0.0;
        Handle(AIS_Shape) shape;
    };

    void showConstructionAxes(const std::vector<Viewport::ConstructionAxisView>& wanted)
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

    void showConstructionPlanes(const std::vector<Viewport::ConstructionPlaneView>& wanted)
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

private:
    struct OriginPlane {
        Handle(AIS_Shape) shape;
        sketch::Plane plane;
        QString name;
        document::OriginPlane kind;
    };

    // Planos XY, XZ e YZ como cuadrados translúcidos en el cuadrante positivo,
    // con un tamaño proporcional a la pieza (como en Fusion 360).
public:
    // Fuera de la elección del plano: a la vista (sin seleccionarse) o no.
    void showOrigin()
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

private:
    void createOriginPlanes()
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

    // Los planos de origen se dibujan en el cuadrante que mira a la cámara
    // (así siempre se alcanzan) y con un tamaño proporcional al zoom: una
    // cuarta parte de lo que se ve. Se rehacen solo si cambia el cuadrante o
    // el tamaño en más de un 15 %.
    void fitOriginPlanes(bool force)
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

    void pickedSketchPlane()
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

public:
    // Rejilla del suelo (fuera del sketch).
    void updateGround(bool force)
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

private:
    // Paso y región de una rejilla según el zoom: la menor potencia de 10
    // cuyas líneas queden a 7 px o más, centrada en lo que se mira. true si
    // cambió.
    bool fitGrid(SketchGrid& target, const sketch::Plane& plane, bool force) const
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

    // La rejilla del sketch.
    void updateGrid(bool force)
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

    Viewport& widget_;
    Handle(AIS_Shape) toolPreview;
    Handle(AIS_Shape) toolEdges;
    Handle(AIS_Shape) toolFaces;
    gp_Pnt handleOrigin;
    gp_Dir handleDirection;
    gp_Dir handleBuiltDirection;  // con la que se construyó la flecha mostrada
    double handleBuiltSize = 1.0;
    bool handlePositive = true;
    AIS_SelectionScheme defaultClickScheme = AIS_SelectionScheme_Replace;
    std::vector<OriginPlane> originPlanes;
    std::vector<ShownPlane> constructionPlanes;
    std::vector<ShownAxis> constructionAxes;
    Handle(SketchPlaneFilter) planeFilter;
    Handle(SketchGrid) grid;
    Handle(SketchGrid) ground;
    Handle(SketchDrawing) drawing;
    Handle(SketchDrawing) preview;
    Handle(AIS_Shape) profiles;
    Graphic3d_ZLayerId regionLayer = Graphic3d_ZLayerId_UNKNOWN;
    std::vector<Handle(AIS_InteractiveObject)> images;  // de referencia, del sketch en curso
    sketch::Plane sketchPlane;
    bool darkTheme = true;
};

} // namespace nuf::render
