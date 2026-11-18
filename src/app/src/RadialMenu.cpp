#include "RadialMenu.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

RadialMenu::RadialMenu(const std::vector<QAction*>& actions, QWidget* parent)
    : QWidget(parent)
    , actions_(actions)
{
    setObjectName(QStringLiteral("radialMenu"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    resize(2 * kOuter + 4, 2 * kOuter + 4);
    hide();
}

void RadialMenu::openAt(const QPoint& center)
{
    const QSize area = parentWidget() != nullptr ? parentWidget()->size() : QSize(2 * kOuter, 2 * kOuter);
    QPoint at = center - QPoint(width() / 2, height() / 2);
    at.setX(std::clamp(at.x(), 0, std::max(0, area.width() - width())));
    at.setY(std::clamp(at.y(), 0, std::max(0, area.height() - height())));
    move(at);
    hover_ = -1;
    show();
    raise();
    setFocus();
}

int RadialMenu::sectorAt(const QPoint& point) const
{
    if (actions_.empty()) {
        return -1;
    }
    const QPointF delta = QPointF(point) - QPointF(width() / 2.0, height() / 2.0);
    const double distance = std::hypot(delta.x(), delta.y());
    if (distance < kInner || distance > kOuter) {
        return -1;
    }
    // El primer sector va centrado arriba y se avanza en sentido horario.
    const double step = 2.0 * std::numbers::pi / double(actions_.size());
    double angle = std::atan2(delta.x(), -delta.y());  // 0 arriba, positivo hacia la derecha
    angle += step / 2.0;
    while (angle < 0.0) {
        angle += 2.0 * std::numbers::pi;
    }
    return int(angle / step) % int(actions_.size());
}

bool RadialMenu::choose(int index)
{
    QAction* action = index >= 0 && index < count() ? actions_[std::size_t(index)] : nullptr;
    const bool run = action != nullptr && action->isEnabled();
    hide();
    deleteLater();
    if (run) {
        action->trigger();
    }
    return run;
}

void RadialMenu::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF center(width() / 2.0, height() / 2.0);
    const double step = 360.0 / double(std::max<std::size_t>(1, actions_.size()));
    const QColor base = palette().color(QPalette::Window);
    const QColor highlight = palette().color(QPalette::Highlight);
    const QColor text = palette().color(QPalette::WindowText);
    for (std::size_t i = 0; i < actions_.size(); ++i) {
        // Qt mide los ángulos desde la derecha y en sentido antihorario: el sector 0 queda centrado arriba.
        const double start = 90.0 - (double(i) + 0.5) * step;
        QPainterPath ring;
        ring.arcMoveTo(QRectF(center.x() - kOuter, center.y() - kOuter, 2.0 * kOuter, 2.0 * kOuter), start);
        ring.arcTo(QRectF(center.x() - kOuter, center.y() - kOuter, 2.0 * kOuter, 2.0 * kOuter), start, -step);
        ring.arcTo(QRectF(center.x() - kInner, center.y() - kInner, 2.0 * kInner, 2.0 * kInner), start - step, step);
        ring.closeSubpath();
        const bool enabled = actions_[i]->isEnabled();
        QColor fill = base;
        fill.setAlpha(enabled ? 235 : 170);
        if (int(i) == hover_ && enabled) {
            fill = highlight;
            fill.setAlpha(230);
        }
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(fill);
        painter.drawPath(ring);

        const double middle = (double(i)) * step * std::numbers::pi / 180.0;
        const double radius = (kOuter + kInner) / 2.0;
        const QPointF spot(center.x() + radius * std::sin(middle), center.y() - radius * std::cos(middle));
        painter.save();
        painter.setOpacity(enabled ? 1.0 : 0.4);
        const QIcon icon = actions_[i]->icon();
        if (!icon.isNull()) {
            icon.paint(&painter, QRect(int(spot.x()) - 14, int(spot.y()) - 22, 28, 28));
        }
        QString name = actions_[i]->text();
        name.remove(QLatin1Char('&'));
        if (!actions_[i]->property("shortName").toString().isEmpty()) {
            name = actions_[i]->property("shortName").toString();
        }
        painter.setPen(int(i) == hover_ && enabled ? palette().color(QPalette::HighlightedText) : text);
        QFont font = painter.font();
        font.setPointSizeF(font.pointSizeF() * 0.85);
        painter.setFont(font);
        painter.drawText(QRectF(spot.x() - 40, spot.y() + 8, 80, 18), Qt::AlignHCenter | Qt::AlignTop, name);
        painter.restore();
    }
    // El centro: cierra el menú.
    painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
    QColor core = base;
    core.setAlpha(235);
    painter.setBrush(core);
    painter.drawEllipse(center, double(kInner) - 6.0, double(kInner) - 6.0);
    painter.setPen(text);
    painter.drawText(QRectF(center.x() - 20, center.y() - 10, 40, 20), Qt::AlignCenter, QStringLiteral("×"));
}

void RadialMenu::mouseMoveEvent(QMouseEvent* event)
{
    const int sector = sectorAt(event->position().toPoint());
    if (sector != hover_) {
        hover_ = sector;
        update();
    }
}

void RadialMenu::mouseReleaseEvent(QMouseEvent* event)
{
    // El primer clic derecho que abrió el menú ya soltó su botón; el siguiente clic (izquierdo o derecho) elige.
    choose(sectorAt(event->position().toPoint()));
}

void RadialMenu::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        deleteLater();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RadialMenu::leaveEvent(QEvent*)
{
    if (hover_ != -1) {
        hover_ = -1;
        update();
    }
}

} // namespace nuf::app
