#include "BusySpinner.hpp"

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nuf::app {

BusySpinner::BusySpinner(QWidget* parent, int size)
    : QWidget(parent)
{
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    timer_.setInterval(80);
    connect(&timer_, &QTimer::timeout, this, [this] {
        phase_ = (phase_ + kSpokes - 1) % kSpokes;  // gira en el sentido de las agujas del reloj
        update();
    });
}

void BusySpinner::setColor(const QColor& color)
{
    color_ = color;
    update();
}

void BusySpinner::showEvent(QShowEvent* event)
{
    timer_.start();
    QWidget::showEvent(event);
}

void BusySpinner::hideEvent(QHideEvent* event)
{
    timer_.stop();
    QWidget::hideEvent(event);
}

void BusySpinner::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const double half = std::min(width(), height()) / 2.0;
    const double outer = half - 1.0;
    const double inner = half * 0.48;
    painter.translate(width() / 2.0, height() / 2.0);
    QPen pen;
    pen.setCapStyle(Qt::RoundCap);
    pen.setWidthF(std::max(1.4, half * 0.17));
    for (int i = 0; i < kSpokes; ++i) {
        // La rayita «cabeza» (la de la fase actual) es la más opaca y las que la siguen se van apagando.
        const int age = (i - phase_ + kSpokes) % kSpokes;
        QColor color = color_;
        color.setAlphaF(0.15 + 0.85 * (1.0 - double(age) / kSpokes));
        pen.setColor(color);
        painter.setPen(pen);
        const double angle = 2.0 * std::numbers::pi * i / kSpokes;
        painter.drawLine(QPointF(inner * std::sin(angle), -inner * std::cos(angle)),
                         QPointF(outer * std::sin(angle), -outer * std::cos(angle)));
    }
}

} // namespace nuf::app
