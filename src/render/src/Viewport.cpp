#include <nuf/render/Viewport.hpp>

#include <QPainter>

namespace nuf::render {

Viewport::Viewport(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
}

void Viewport::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    // Fondo del mockup en claro; un gris azulado oscuro en el tema oscuro.
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    painter.fillRect(rect(), dark ? QColor(0x2B, 0x2F, 0x36) : QColor(0xE6, 0xE9, 0xED));
    painter.setPen(palette().color(QPalette::PlaceholderText));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("Viewport 3D\n(se implementa en la Fase 1)"));
}

} // namespace nuf::render
