#pragma once

#include <QWidget>

#include <vector>

class QAction;

namespace nuf::app {

// Menú radial (U6): un anillo con las acciones más usadas alrededor del cursor, que se abre con clic derecho sobre la
// vista. Cada sector lleva el icono y el nombre de una acción; se elige con un clic y se cierra con un clic en el
// centro, con Esc o al elegir. Las acciones desactivadas salen apagadas y no responden.
class RadialMenu final : public QWidget {
    Q_OBJECT

public:
    RadialMenu(const std::vector<QAction*>& actions, QWidget* parent);

    // Abre el anillo centrado en «center» (coordenadas del padre), corrido si no cabe.
    void openAt(const QPoint& center);
    // Cuántos sectores y cuál está bajo el punto (coordenadas del menú); -1 si ninguno.
    int count() const { return int(actions_.size()); }
    int sectorAt(const QPoint& point) const;
    // Elige el sector (como un clic): lanza la acción si está activa y cierra. Devuelve si lanzó algo.
    bool choose(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    static constexpr int kOuter = 140;  // radio exterior del anillo
    static constexpr int kInner = 44;   // radio interior (el centro cierra el menú)

    std::vector<QAction*> actions_;
    int hover_ = -1;
};

} // namespace nuf::app
