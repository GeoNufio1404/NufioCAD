#pragma once

#include <QColor>
#include <QTimer>
#include <QWidget>

namespace nuf::app {

// Indicador de «algo está cargando»: doce rayitas que giran con una estela que se desvanece (como el icono de carga
// de siempre). No mide porcentaje: solo dice que hay trabajo en marcha. Gira solo mientras es visible (cuando se oculta
// se detiene el temporizador), así que no cuesta nada en reposo.
class BusySpinner final : public QWidget {
    Q_OBJECT

public:
    explicit BusySpinner(QWidget* parent = nullptr, int size = 18);

    void setColor(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    static constexpr int kSpokes = 12;

    QTimer timer_;
    int phase_ = 0;
    QColor color_{Qt::white};
};

} // namespace nuf::app
