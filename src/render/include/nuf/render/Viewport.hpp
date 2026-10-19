#pragma once

#include <QOpenGLWidget>
#include <QSurfaceFormat>
#include <QString>

#include <memory>

namespace nuf::render {

// Viewport 3D: visor V3d/AIS de OpenCASCADE dentro de un QOpenGLWidget.
// Navegación:
//   arrastre izquierdo           orbitar
//   clic izquierdo               seleccionar cara o arista
//   Shift + clic izquierdo       agregar/quitar de la selección
//   Shift + arrastre izquierdo   selección por rectángulo
//   botón central                desplazar (pan)
//   Shift + botón central        orbitar (como en Fusion 360)
//   rueda                        zoom hacia el cursor
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

signals:
    // Texto breve para la barra de estado ("Cara seleccionada", "2 aristas"…).
    void selectionChanged(const QString& description);

protected:
    void initializeGL() override;
    void paintGL() override;
    void changeEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    class Impl;
    friend class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nuf::render
