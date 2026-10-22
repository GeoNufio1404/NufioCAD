#pragma once

#include <nuf/sketch/Plane.hpp>

#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace nuf::render {

// Qué resaltar al dibujar el sketch.
struct SketchHighlight {
    std::vector<sketch::Ref> selected;
    sketch::Ref hovered;
    std::vector<sketch::ConstraintId> selectedConstraints;
    sketch::ConstraintId hoveredConstraint = 0;
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
    void showFinishedSketch(int id, const QString& name, const sketch::Sketch& sketch, const sketch::Plane& plane);
    void hideFinishedSketch(int id);
    bool isSketching() const;
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
    std::vector<sketch::Geometry> textOutline(const QString& text, double height) const;
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
    void selectionChanged(const QString& description);
    // Doble clic sobre un sketch terminado (para volver a editarlo).
    void finishedSketchActivated(int id);
    // description: "Plano XY", "Cara plana"…
    void sketchPlanePicked(const nuf::sketch::Plane& plane, const QString& description);
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
    void sketchEdgeProjected(const std::vector<nuf::sketch::Geometry>& geometry);
    // Shift + arrastre sin herramienta: rectángulo de selección (esquinas).
    void sketchBoxSelected(double x0, double y0, double x1, double y1);
    // Arrastre izquierdo con una herramienta activa: desde (x0, y0), donde
    // se pulsó, hasta (x, y). Al soltar se emite DragReleased en vez de un clic.
    void sketchToolDragged(double x0, double y0, double x, double y);
    void sketchToolDragReleased(double x0, double y0, double x, double y);

protected:
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
