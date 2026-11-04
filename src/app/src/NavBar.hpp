#pragma once

#include <QWidget>

#include <vector>

class QAction;

namespace nuf::app {

// Barra de navegación flotante al estilo de Fusion 360: una píldora con
// botones de icono, centrada abajo sobre el visor. Se coloca sola y sigue al
// visor cuando cambia de tamaño.
class NavBar final : public QWidget {
    Q_OBJECT

public:
    // `viewport` es el padre sobre el que flota.
    NavBar(QWidget* viewport, const std::vector<QAction*>& actions);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void reposition();
};

} // namespace nuf::app
