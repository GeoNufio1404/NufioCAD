#pragma once

#include <nuf/document/Features.hpp>
#include <nuf/sketch/Plane.hpp>

#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QString>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nuf::render {

// Qué resaltar al dibujar el sketch.
struct SketchHighlight {
    std::vector<sketch::Ref> selected;
    sketch::Ref hovered;
    std::vector<sketch::ConstraintId> selectedConstraints;
    sketch::ConstraintId hoveredConstraint = 0;
};

// Lo seleccionado en el modelo (fuera del sketch), con referencias estables.
struct ModelSelection {
    struct Profile {
        document::FeatureId sketch = 0;
        int index = 0;  // en el orden de sketch::findProfiles
    };
    struct Curve {
        document::FeatureId sketch = 0;
        sketch::EntityId entity = 0;
    };
    struct Face {
        std::string body;  // Body::key
        geometry::ElementRef ref;
    };
    struct Edge {
        std::string body;
        geometry::ElementRef ref;
    };
    std::vector<Profile> profiles;
    std::vector<Curve> curves;
    std::vector<Face> faces;
    std::vector<Edge> edges;

    bool empty() const { return profiles.empty() && curves.empty() && faces.empty() && edges.empty(); }
};

// Viewport 3D: visor V3d/AIS de OpenCASCADE dentro de un QOpenGLWidget.
// Navegación:
//   arrastre izquierdo           orbitar
//   clic izquierdo               seleccionar cara o arista
//   Shift + clic izquierdo       agregar/quitar de la selección
//   Shift + arrastre izquierdo   selección por rectángulo
//   arrastre derecho             desplazar (pan); el clic derecho queda
//                                libre para el menú rápido
//   botón central                desplazar (alternativa)
//   Shift + botón central        orbitar (como en Fusion 360)
// Las herramientas Orbitar / Desplazar / Zoom (setNavigationMode) hacen que el
// arrastre izquierdo haga solo eso.
//   rueda                        zoom hacia el cursor
// Modo sketch: pickSketchPlane() muestra los planos de origen y deja elegir
// uno o una cara plana; beginSketch() orienta la cámara y muestra la rejilla.
// Las cabeceras de OCCT quedan en el .cpp para que app/ no dependa de ellas.
class Viewport final : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    // Formato OpenGL que necesita OCCT. Debe fijarse como formato por defecto
    // antes de crear QApplication, para que la ventana principal también lo use.
    static QSurfaceFormat defaultFormat();

    // Encuadra todo el modelo.
    void fitAll();
    // Vista isométrica inicial, encuadrando el modelo.
    void resetView();

    // Muestra los planos de origen y deja elegir uno o una cara plana;
    // al hacerlo emite sketchPlanePicked.
    void pickSketchPlane();
    void cancelPickSketchPlane();
    bool isPickingSketchPlane() const;

    // Entra al sketch: cámara de frente al plano, rejilla y coordenadas.
    void beginSketch(const sketch::Plane& plane);
    void endSketch();
    // Muestra un sketch terminado en su plano: sus regiones (perfiles) y sus
    // curvas se pueden seleccionar en la vista 3D, por encima de la pieza.
    // Con el mismo id, sustituye al anterior.
    void showFinishedSketch(document::FeatureId id, const QString& name, const sketch::Sketch& sketch,
                            const sketch::Plane& plane);
    void hideFinishedSketch(document::FeatureId id);

    // Planos de construcción a la vista (cuadrados translúcidos). Se pueden
    // elegir como plano de un sketch al usar pickSketchPlane().
    struct ConstructionPlaneView {
        document::FeatureId id = 0;
        QString name;
        sketch::Plane plane;
    };
    void showConstructionPlanes(const std::vector<ConstructionPlaneView>& planes);
    // Ejes de construcción a la vista (una línea turquesa que atraviesa la pieza).
    struct ConstructionAxisView {
        document::FeatureId id = 0;
        QString name;
        sketch::Vec3 point;
        sketch::Vec3 direction;
    };
    void showConstructionAxes(const std::vector<ConstructionAxisView>& axes);

    // Planos de origen siempre a la vista (no solo al elegir el plano).
    void setOriginVisible(bool visible);
    // Rotula en el sketch el largo de las líneas y el Ø o R de círculos y arcos
    // (lo que no tiene cota propia).
    void setSketchMeasuresVisible(bool visible);
    // Por defecto las medidas y las restricciones solo se ven en lo que tiene el
    // cursor encima o está seleccionado (así el sketch no se satura); estas
    // opciones las muestran todas a la vez.
    void setSketchShowAll(bool measures, bool constraints);
    // Rejilla del suelo (plano XY) fuera del sketch.
    void setGroundVisible(bool visible);

    // Cuerpos del modelo. Solo se rehacen los que cambiaron.
    void showBodies(const std::vector<document::Body>& bodies);
    // Sección: corta la vista de los cuerpos con un plano (la parte que cae del lado de la normal se
    // oculta) y tapa el corte. No cambia el modelo.
    void setSectionPlane(const sketch::Vec3& origin, const sketch::Vec3& normal);
    void clearSectionPlane();
    // Sólido de la operación en curso, translúcido (rojo si quita material).
    void showToolPreview(const geometry::Shape& tool, bool removes);
    // Resalta aristas de un cuerpo (por índice) en azul; clearToolPreview() las quita.
    void showToolEdges(const geometry::Shape& body, const std::vector<int>& edges);
    // Resalta caras de un cuerpo (por índice) en azul translúcido; clearToolPreview() las quita.
    void showToolFaces(const geometry::Shape& body, const std::vector<int>& faces);
    void clearToolPreview();

    ModelSelection modelSelection() const;
    void clearModelSelection();
    // Selecciona esas regiones de sketches terminados (al editar una operación).
    void selectProfiles(const std::vector<ModelSelection::Profile>& profiles);
    // Con una operación en curso, un clic agrega o quita de la selección
    // (sin Shift), como en Fusion 360.
    void setAdditiveSelection(bool on);

    // Flecha para arrastrar la distancia de una extrusión: sale de «origin»
    // en «direction» y su punta está a «distance». Al arrastrarla se emite
    // distanceHandleDragged con la nueva distancia.
    void showDistanceHandle(const sketch::Vec3& origin, const sketch::Vec3& direction, double distance);
    void hideDistanceHandle();
    // --- Mover y girar con el ratón (como Orca Slicer) ---
    // Tres flechas (X rojo, Y verde, Z azul) para mover, o tres anillos con marcas cada 45° para girar, centrados en
    // «origin». Al arrastrar uno se emite gizmoDragged(eje, cantidad): lo que se ha desplazado (mm) o girado (rad,
    // acumulado) desde que se agarró; el modelo no se recalcula, quien escucha mueve una «sombra» (showGhost) y
    // al soltar (gizmoReleased) calcula.
    enum class GizmoKind { None, Move, Rotate };
    void showGizmo(GizmoKind kind, const sketch::Vec3& origin);
    void hideGizmo();
    // Una copia translúcida del cuerpo, que se coloca con setGhostMotion sin recalcular nada: gira alrededor de
    // los ejes X, Y y Z (en ese orden) por «pivot» y luego se desplaza.
    void showGhost(const std::vector<geometry::Shape>& bodies);
    void setGhostMotion(const sketch::Vec3& pivot, const sketch::Vec3& rotation, const sketch::Vec3& translation);
    void hideGhost();
    // El arco de lo que se lleva girado en un arrastre (de «start» a «angle» rad alrededor del eje por «center»): una
    // sola arista gruesa, lo bastante barata para rehacerla a cada paso del ratón.
    void showAngleSweep(const sketch::Vec3& center, const sketch::Vec3& axis, const sketch::Vec3& start, double angle);
    void hideAngleSweep();
    // Una recta gruesa y brillante entre dos puntos: la guía de «estás alineado con esta marca» al girar con un anillo.
    void showAlignmentLine(const sketch::Vec3& from, const sketch::Vec3& to);
    void hideAlignmentLine();
    // Flecha de doble punta entre dos puntos del modelo: señala la medida que se está escribiendo en el panel de una
    // herramienta. No se puede arrastrar ni seleccionar.
    void showDimensionArrow(const sketch::Vec3& from, const sketch::Vec3& to);
    // Lo mismo para un ángulo: arco de doble punta alrededor del eje (que pasa por «center» con dirección «axis»),
    // desde el punto «start» y girando «angle» radianes (con signo).
    void showDimensionArc(const sketch::Vec3& center, const sketch::Vec3& axis, const sketch::Vec3& start, double angle);
    void hideDimensionArrow();
    bool isSketching() const;

    // Herramienta de navegación (barra inferior): mientras está activa, el
    // arrastre izquierdo orbita, desplaza o hace zoom, sin seleccionar ni dibujar.
    enum class NavigationMode { Default, Orbit, Pan, Zoom };
    void setNavigationMode(NavigationMode mode);
    // Vuelve a poner la cámara de frente al plano del sketch.
    void lookAtSketch();

    // Dibuja la geometría del sketch, sus restricciones (iconos) y cotas; las
    // entidades de «underconstrained» (aún libres) en azul y el resto como
    // totalmente restringidas. Las anotaciones se recolocan al hacer zoom.
    void showSketch(const sketch::Sketch& sketch, const std::vector<sketch::EntityId>& underconstrained,
                    const SketchHighlight& highlight = {});
    // Vista previa de la herramienta activa y marcador del punto de ajuste.
    void showSketchPreview(const std::vector<sketch::Geometry>& geometry, std::optional<sketch::Vec2> snapPoint);
    // En el sketch, un clic izquierdo (sin arrastrar) emite sketchClicked y
    // un doble clic, sketchDoubleClicked. Con una herramienta de dibujo
    // activa, además, el arrastre izquierdo no orbita.
    void setSketchToolActive(bool active);
    // Proyectar: las aristas de la pieza se resaltan y un clic en una emite
    // sketchEdgeProjected con su proyección sobre el plano del sketch.
    void setSketchProjectMode(bool on);
    // Intersección de la pieza con el plano del sketch (en coordenadas del sketch).
    std::vector<sketch::Geometry> sketchSection() const;
    // Contorno de un texto con la fuente de la interfaz, en coordenadas del
    // sketch con la línea base en el origen. Vacío si no hay fuente.
    // family: la fuente (vacía = la de la interfaz).
    std::vector<sketch::Geometry> textOutline(const QString& text, double height, const QString& family = {}) const;
    // Imagen de referencia (para calcar) en el plano del sketch, con su
    // esquina inferior izquierda en «corner»; se quita al terminar el sketch.
    bool showSketchImage(const QString& path, sketch::Vec2 corner, double width, double height);

    // Llamado al recibir sketchPressed: este arrastre izquierdo mueve
    // geometría (sketchDragged / sketchDragFinished) en lugar de orbitar.
    void grabSketchDrag();
    // Tamaño de un píxel en el plano del sketch (mm) y paso de la rejilla.
    double sketchPixelSize() const;
    double sketchGridStep() const;

signals:
    // Texto breve para la barra de estado ("Cara seleccionada", "2 aristas"…).
    // Se está preparando en segundo plano el dibujo de un cuerpo pesado (una rosca): mientras tanto se
    // sigue viendo lo anterior.
    void busyChanged(bool busy);
    void selectionChanged(const QString& description);
    // Cambió la selección del modelo (ver modelSelection()).
    void modelSelectionChanged();
    // Doble clic sobre un sketch terminado (para volver a editarlo).
    void finishedSketchActivated(nuf::document::FeatureId id);
    // description: "Plano XY", "Cara plana"…; attachment: dónde queda sujeto.
    void sketchPlanePicked(const nuf::sketch::Plane& plane, const QString& description,
                           const nuf::document::SketchAttachment& attachment);
    void distanceHandleDragged(double distance);
    void distanceHandleReleased();
    // Se agarró la flecha (o el anillo) «axis» (0 = X, 1 = Y, 2 = Z), se está arrastrando y se soltó.
    void gizmoPressed(int axis);
    void gizmoDragged(int axis, double amount);
    void gizmoReleased(int axis);
    // Posición del cursor en coordenadas del sketch (mm); hasPoint = false
    // si el cursor no corta el plano (vista de canto o fuera del visor).
    void sketchCursorMoved(bool hasPoint, double x, double y);
    void sketchClicked(double x, double y, Qt::KeyboardModifiers modifiers);
    void sketchDoubleClicked(double x, double y);
    // Botón izquierdo pulsado en el sketch sin herramienta activa.
    void sketchPressed(double x, double y);
    void sketchDragged(double x, double y);
    void sketchDragFinished();
    // Tecla de un número (o «.», «,», «-») pulsada en el sketch: valores
    // escritos junto al cursor.
    void sketchTyped(const QString& text);
    // Enter en el sketch (terminar una spline, por ejemplo).
    void sketchEnterPressed();
    // source: la arista de la que sale (para vincular la proyección).
    void sketchEdgeProjected(const std::vector<nuf::sketch::Geometry>& geometry,
                             const nuf::geometry::ElementRef& source);
    // Shift + arrastre sin herramienta: rectángulo de selección (esquinas).
    void sketchBoxSelected(double x0, double y0, double x1, double y1);
    // Arrastre izquierdo con una herramienta activa: desde (x0, y0), donde
    // se pulsó, hasta (x, y). Al soltar se emite DragReleased en vez de un clic.
    void sketchToolDragged(double x0, double y0, double x, double y);
    void sketchToolDragReleased(double x0, double y0, double x, double y);

protected:
    // Entrega al que escucha el último movimiento pendiente de un arrastre de flecha o de anillo.
    void flushGizmoDrag();
    void initializeGL() override;
    void paintGL() override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Botones que se pasan a OCCT (sin el izquierdo si lo usa una herramienta).
    Qt::MouseButtons navigationButtons(Qt::MouseButtons buttons) const;

    class Impl;
    friend class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nuf::render
