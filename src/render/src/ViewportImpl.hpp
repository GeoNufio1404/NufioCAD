#pragma once

// El Impl del visor (estado y manejo de OCCT). Solo lo incluye Viewport*.cpp.

#include "ViewportDrawing.hpp"

#include <optional>
#include <functional>

#include <StdPrs_ToolTriangulatedShape.hxx>

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
    static void pullTowardCamera(const Handle(AIS_Shape)& shape);

    enum class Mode { Model, PickPlane, Sketch };

    void applyTheme(bool dark);
    Quantity_Color toolPreviewColor() const;

    void describeSelection();

    // --- Cuerpos del modelo ------------------------------------------------

    struct ShownBody {
        std::string key;
        geometry::Shape shape;
        Handle(AIS_Shape) ais;
        std::string color;  // el del usuario («#rrggbb») con el que se creó
    };

    // --- Mallado en segundo plano ---------------------------------------------
    // Los cuerpos pesados (una rosca) tardan segundos en mallarse y en calcular sus normales: se hace en
    // un hilo de trabajo sobre una copia, y mientras tanto se sigue viendo lo anterior.

    struct MeshTask {
        std::uint64_t id = 0;
        geometry::Shape shape;
        geometry::PreparedMesh mesh;
        geometry::TaskPool::TaskPtr task;  // en el grupo de hilos de trabajo
    };
    std::vector<std::unique_ptr<MeshTask>> meshTasks;
    std::vector<std::unique_ptr<MeshTask>> cancelledTasks;  // canceladas que aún no han terminado
    std::vector<document::Body> wantedBodies;               // la última lista pedida
    std::uint64_t nextMeshTaskId = 1;
    std::vector<const void*> unmeshable;                    // su malla en segundo plano falló: se dibujan como siempre
    bool busy = false;
    bool waitingForMesh = false;      // hay cuerpos esperando su malla (aunque «busy» aún no se haya avisado)
    bool fitWhenReady = false;        // se pidió encuadrar mientras faltaban cuerpos por mostrar: se hace al tenerlos
    std::uint64_t busyToken = 0;
    geometry::Shape pendingTool;      // el sólido de la vista previa de una herramienta, a la espera de su malla
    bool pendingToolRemoves = false;

    void setBusy(bool on);

    // El aviso de «preparando el dibujo» solo sale si la espera pasa de 150 ms: con piezas sencillas la malla llega
    // antes y la barra de estado no parpadea en cada paso de una vista previa.
    void setWaiting(bool waiting);

    // Con qué desviación y ángulo se malla un cuerpo: los mismos que usaría el visor al dibujarlo (relativos al tamaño
    // de la pieza, 0,001 por defecto) o, si es una rosca, los gruesos de siempre. Así lo que sale se ve igual.
    struct BodyMeshing {
        double deviation = 0.0;
        double angle = 0.0;
    };

    BodyMeshing meshingFor(const geometry::Shape& shape) const;

    // ¿Falta mallar esta forma? (si no tiene malla y no falló antes)
    bool needsMesh(const geometry::Shape& shape) const;

    void startMeshTask(const geometry::Shape& shape);

    // En el hilo de la interfaz: la malla ya está; se pasa al cuerpo y se vuelve a pintar.
    void meshTaskFinished(std::uint64_t id);

    void reapCancelled();

    void cancelMeshTasks();

    void showBodies(const std::vector<document::Body>& list);

    // --- Sección -------------------------------------------------------------

    Handle(Graphic3d_ClipPlane) clipSection;

    void setSection(const sketch::Vec3& origin, const sketch::Vec3& normal);

    void clearSection();

    // Modos de selección de un cuerpo según lo que se esté haciendo.
    void activateBody(const ShownBody& b);

    void activateBodies();

    const ShownBody* bodyOf(const Handle(AIS_InteractiveObject)& object) const;

    // Todos los cuerpos en una forma (para cortar con el plano, medir…).
    TopoDS_Shape allBodies() const;

    // El sólido que se quita o se agrega se dibuja cuando su malla está lista (se prepara en segundo plano, como la
    // de los cuerpos): lo que se ve mientras tanto es el cuerpo de la vista previa, sin la sombra roja.
    void showToolPreview(const geometry::Shape& tool, bool removes);

    void showPendingTool();

    void showToolEdges(const geometry::Shape& body, const std::vector<int>& edges);
    void showToolPicks(const std::vector<std::pair<geometry::Shape, int>>& faces,
                       const std::vector<std::pair<geometry::Shape, int>>& edges);

    void showToolFaces(const geometry::Shape& body, const std::vector<int>& faces);

    void clearToolPreview();

    ModelSelection modelSelection() const;

    void setAdditiveSelection(bool on);

    // Ayuda para elegir aristas: tolerancia de captura más ancha y la arista cercana marcada (ver Viewport::setEdgeAssist).
    bool edgeAssist = false;
    Handle(AIS_Shape) hoverEdge;
    TopoDS_Shape hoverEdgeShape;
    void setEdgeAssist(bool on);
    bool updateEdgeHover();  // true si cambió la arista marcada
    void hideEdgeHover();

    // --- Flecha de distancia -------------------------------------------------

    void showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance);

    void hideDistanceHandle();

    // --- Mover y girar con el ratón ---------------------------------------------

    static gp_Dir gizmoAxis(int k) { return k == 0 ? gp_Dir(1, 0, 0) : k == 1 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1); }
    // De dónde se mide el ángulo de cada anillo: X de Y hacia Z, Y de Z hacia X, Z de X hacia Y.
    static gp_Dir gizmoRef(int k) { return k == 0 ? gp_Dir(0, 1, 0) : k == 1 ? gp_Dir(0, 0, 1) : gp_Dir(1, 0, 0); }

    Viewport::GizmoKind gizmoKind = Viewport::GizmoKind::None;
    Handle(AIS_Shape) gizmoShapes[3];
    int gizmoMask = 7;  // los ejes que se dibujan (bit k = eje k)
    bool gizmoBuilt() const { return !gizmoShapes[0].IsNull() || !gizmoShapes[1].IsNull() || !gizmoShapes[2].IsNull(); }
    gp_Pnt gizmoOrigin;
    double gizmoBuiltSize = 0.0;
    // Arrastrar piezas (Fase 21)
    bool bodyDragEnabled = false;
    std::function<bool(const std::string&)> bodyDragFilter;
    bool bodyDragging = false;
    std::string dragKey;
    std::optional<gp_Pnt> lastPick;  // el punto 3D donde cayó el último clic que seleccionó algo
    gp_Pnt dragGrab;      // el punto del cuerpo que se agarró
    gp_Dir dragNormal;    // la dirección de la vista: el plano del arrastre pasa por «dragGrab» perpendicular a ella
    QPointF dragPress;
    bool dragMoved = false;
    std::optional<gp_Pnt> dragPlanePoint(const Vec2i& pixel) const;
    int gizmoActive = -1;  // el eje que se está arrastrando
    gp_Pnt gizmoDragOrigin;  // dónde estaba el centro al agarrar (el arrastre se mide desde ahí)
    double gizmoGrab = 0.0;  // mover: parámetro sobre el eje al agarrar; girar: ángulo anterior
    double gizmoTotal = 0.0;
    bool gizmoHiddenForZoom = false;
    double gizmoLastPixel = 0.0;
    QTimer* gizmoSettle = nullptr;
    Handle(AIS_Shape) ghost;

    void clearGizmoShapes();

    void hideGizmo();

    // Las flechas y los anillos guardan su tamaño en pantalla, pero rehacerlos a cada cuadro de un zoom (decenas de
    // sólidos) deja la vista a 1 imagen por segundo: al cambiar el zoom se quitan y se vuelven a dibujar cuando la
    // vista lleva un momento quieta.
    void updateGizmoForZoom();

    void showGizmo(Viewport::GizmoKind kind, const sketch::Vec3& origin);

    // Parámetro sobre el eje «k» que pasa por «origin» del punto más cercano al rayo del píxel.
    std::optional<double> gizmoLineParameter(const Vec2i& pixel, const gp_Pnt& origin, int k) const;

    // Ángulo del punto del plano del anillo «k» al que apunta el píxel, medido desde la marca 0 del anillo.
    std::optional<double> gizmoRingAngle(const Vec2i& pixel, const gp_Pnt& origin, int k) const;

    // Mientras se arrastra una flecha o un anillo, los otros dos se quitan (la vista queda limpia para ver cómo quedaría);
    // al soltar vuelven. Quitarlos de la vista no los destruye: se vuelven a mostrar tal cual.
    void hideOtherGizmoShapes(int keep);

    void restoreGizmoShapes();

    bool startGizmoDrag(int k, const Vec2i& pixel);

    // Lo arrastrado hasta ahora (mm o rad), o nada si el ratón apunta a un lugar sin respuesta (plano de canto).
    std::optional<double> gizmoDragAmount(const Vec2i& pixel);

    void hideGhost();

    void showGhost(const std::vector<geometry::Shape>& list, bool alert = false);

    void setGhostMotion(const sketch::Vec3& pivot, const sketch::Vec3& rotation, const sketch::Vec3& translation);

    // El arco de lo que se lleva girado en un arrastre: una sola arista gruesa desde «start» hasta «angle» (rad), alrededor
    // del eje que pasa por «center». Se rehace a cada paso del ratón, así que tiene que ser barata (el arco con
    // cilindros y puntas del dimensionamiento son decenas de sólidos).
    Handle(AIS_Shape) sweepShape;

    void hideAngleSweep();

    void showAngleSweep(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle);

    Handle(AIS_Shape) alignShape;

    void hideAlignmentLine();

    // La guía de alineación: una recta entre dos puntos (a través del centro del anillo, en el plano del anillo).
    void showAlignmentLine(const sketch::Vec3& from, const sketch::Vec3& to);

    // Los movimientos del ratón de un arrastre llegan más deprisa de lo que se dibuja: se agrupan (el último vale).
    QTimer* gizmoFlush = nullptr;
    QElapsedTimer gizmoClock;
    double gizmoPending = 0.0;
    bool gizmoHasPending = false;

    // --- Flecha de medida (doble punta) -----------------------------------------

    void showDimensionArrow(const sketch::Vec3& from, const sketch::Vec3& to);

    void showDimensionArc(const sketch::Vec3& centerPoint, const sketch::Vec3& axisDir, const sketch::Vec3& startPoint, double angle);

    void hideDimensionArrow();

    void removeHandle();

    // Parámetro sobre el eje de la flecha del punto más cercano al rayo del píxel.
    std::optional<double> handleParameterAt(const Vec2i& pixel) const;

    // --- Modo sketch -------------------------------------------------------

    void pickSketchPlane();

    void cancelPickSketchPlane();

    void beginSketch(const sketch::Plane& plane);

    void endSketch();

    // Anima la cámara hasta mirar el plano de frente, con el eje Y del sketch
    // hacia arriba, sin cambiar el zoom ni alejarse del punto que se miraba.
    void lookAtSketch();

    // Punto del plano del sketch bajo el píxel, si el rayo lo corta.
    std::optional<sketch::Vec2> sketchPointAt(const Vec2i& pixel) const;

    // Colores de la geometría del sketch (como en Fusion: azul = aún libre).
    SketchStroke freeStroke() const { return {darkTheme ? rgb(0x6C, 0xA6, 0xFF) : rgb(0x1F, 0x6F, 0xEB)}; }
    SketchStroke fixedStroke() const { return {darkTheme ? rgb(0xE6, 0xE9, 0xED) : rgb(0x1F, 0x23, 0x29)}; }
    SketchStroke constructionStroke() const { return {rgb(0xF2, 0xA3, 0x4B), 1.5, Aspect_TOL_DASH}; }

    static Quantity_Color selectedColor() { return rgb(0xFF, 0xAA, 0x33); }
    static Quantity_Color hoveredColor() { return rgb(0xFF, 0xD5, 0x8A); }

    void showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
                    const SketchHighlight& highlight);

    // Las regiones cerradas del sketch mostrado, sombreadas.
    void rebuildProfiles();

    // Medida de 12.50 mm como «12.5» (sin ceros de sobra).
    static std::string formatMeasure(double value);

    // Largo de cada línea, Ø de cada círculo y R de cada arco, para tener
    // referencia del tamaño sin tener que acotar. Lo que ya tiene una cota
    // propia no se repite, y lo proyectado (cientos de curvas) ni la
    // construcción se rotulan.
    void addMeasures(const sketch::Sketch& sketch, const std::set<sketch::EntityId>& focus,
                     const Quantity_Color& color, const Quantity_Color& focused);

    // Vuelve a dibujar el último sketch mostrado (también al cambiar el zoom,
    // porque las anotaciones se colocan en píxeles).
    void rebuildSketch();

    void showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint,
                           const std::vector<std::pair<sketch::Vec2, sketch::Vec2>>& guides,
                           const std::string& badge, sketch::Vec2 badgeAt, bool markerSquare,
                           const std::vector<sketch::Vec2>& awake);

    double pixelSize() const;

    double gridStep() const { return grid->step; }

    void setProjectMode(bool on);

    // Arista de la pieza bajo el píxel, proyectada, y su referencia.
    std::pair<std::vector<sketch::Geometry>, geometry::ElementRef> projectionAt(const Vec2i& pixel);

    std::vector<sketch::Geometry> textOutline(const QString& text, double height, const QString& family) const;

    bool showImage(const QString& path, sketch::Vec2 corner, double width, double height);

    std::vector<sketch::Geometry> section() const;

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
                            const sketch::Plane& plane);

    void hideFinishedSketch(document::FeatureId id);

    // Sketch terminado al que pertenece un objeto, y si es una región.
    std::optional<std::pair<document::FeatureId, bool>> finishedOwner(const Handle(AIS_InteractiveObject)& object) const;

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
    QElapsedTimer profilesClock;           // cuándo se rehicieron por última vez (para no hacerlo más de 10 veces por segundo)
    QTimer* profilesTimer = nullptr;       // rehace los perfiles con lo último cuando pasa el tiempo
    bool profilesPlaneDirty = true;        // el plano cambió: hay que rehacerlos aunque la geometría sea la misma
    std::vector<sketch::EntityId> shownUnderconstrained;
    SketchHighlight shownHighlight;
    double annotationScale = 0.0;
    sketch::LayoutMemory layoutMemory;  // nivel elegido por cota entre repintados
    bool measuresVisible = true;
    bool pointsVisible = true;  // los extremos y centros de las curvas del sketch (U4)
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
                          const Handle(V3d_View)& v) override;

    void OnSelectionChanged(const Handle(AIS_InteractiveContext)& /*ctx*/,
                            const Handle(V3d_View)& /*v*/) override;

public:
    void setNavigationMode(Viewport::NavigationMode newMode);

private:
    void configureNavigation();

    void configureHighlight();

    void createViewCube();

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
    double referenceHalf() const;

    struct ShownAxis {
        document::FeatureId id = 0;
        sketch::Vec3 point;
        sketch::Vec3 direction;
        double half = 0.0;
        Handle(AIS_Shape) shape;
    };

    void showConstructionAxes(const std::vector<Viewport::ConstructionAxisView>& wanted);

    void showConstructionPlanes(const std::vector<Viewport::ConstructionPlaneView>& wanted);

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
    void showOrigin();

private:
    void createOriginPlanes();

    // Los planos de origen se dibujan en el cuadrante que mira a la cámara
    // (así siempre se alcanzan) y con un tamaño proporcional al zoom: una
    // cuarta parte de lo que se ve. Se rehacen solo si cambia el cuadrante o
    // el tamaño en más de un 15 %.
    void fitOriginPlanes(bool force);

    void pickedSketchPlane();

public:
    // Rejilla del suelo (fuera del sketch).
    void updateGround(bool force);

private:
    // Paso y región de una rejilla según el zoom: la menor potencia de 10
    // cuyas líneas queden a 7 px o más, centrada en lo que se mira. true si
    // cambió.
    bool fitGrid(SketchGrid& target, const sketch::Plane& plane, bool force) const;

    // La rejilla del sketch.
    void updateGrid(bool force);

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
