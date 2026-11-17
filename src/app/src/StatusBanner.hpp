#pragma once

#include <QStatusBar>

namespace nuf::app {

class BusySpinner;

// La barra de estado con el spinner de carga lo más a la izquierda posible y, a su derecha, el mensaje. QStatusBar
// dibuja siempre el mensaje a 6 px del borde (no hay forma de correrlo), así que aquí se dibuja a mano: el fondo lo
// sigue poniendo la hoja de estilo, y el texto se corre lo que ocupa el spinner mientras gira.
class StatusBanner final : public QStatusBar {
    Q_OBJECT

public:
    explicit StatusBanner(QWidget* parent = nullptr);

    BusySpinner* spinner() const { return spinner_; }
    // Muestra u oculta el spinner (y corre el mensaje).
    void setBusy(bool busy);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    static constexpr int kMargin = 6;   // lo que QStatusBar deja a la izquierda del mensaje
    static constexpr int kGap = 8;      // entre el spinner y el texto

    void placeSpinner();

    BusySpinner* spinner_ = nullptr;
};

} // namespace nuf::app
