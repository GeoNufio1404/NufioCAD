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
    painter.fillRect(rect(), QColor(0xE6, 0xE9, 0xED));
    painter.setPen(QColor(0x5B, 0x64, 0x70));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("Viewport 3D\n(se implementa en la Fase 1)"));
}

} // namespace nuf::render
