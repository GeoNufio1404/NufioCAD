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
    // Tamaño de un píxel en el plano del sketch (mm) y paso de la rejilla.
    double sketchPixelSize() const;
    double sketchGridStep() const;

signals:
    // Texto breve para la barra de estado ("Cara seleccionada", "2 aristas"…).
    void selectionChanged(const QString& description);
    // description: "Plano XY", "Cara plana"…
    void sketchPlanePicked(const nuf::sketch::Plane& plane, const QString& description);
    // Posición del cursor en coordenadas del sketch (mm); hasPoint = false
    // si el cursor no corta el plano (vista de canto o fuera del visor).
    void sketchCursorMoved(bool hasPoint, double x, double y);
    void sketchClicked(double x, double y, Qt::KeyboardModifiers modifiers);
    void sketchDoubleClicked(double x, double y);

protected:
    void initializeGL() override;
    void paintGL() override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
