#pragma once

#include <QWidget>

namespace nuf::render {

// Marcador de posición del viewport 3D (se implementa en la Fase 1).
class Viewport final : public QWidget {
public:
    explicit Viewport(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace nuf::render
