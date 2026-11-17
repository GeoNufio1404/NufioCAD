#include "StatusBanner.hpp"

#include "BusySpinner.hpp"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

namespace nuf::app {

StatusBanner::StatusBanner(QWidget* parent)
    : QStatusBar(parent)
{
    spinner_ = new BusySpinner(this, 16);
    spinner_->setColor(Qt::white);
    spinner_->hide();
}

void StatusBanner::setBusy(bool busy)
{
    spinner_->setVisible(busy);
    placeSpinner();
    update();
}

void StatusBanner::placeSpinner()
{
    spinner_->move(kMargin, (height() - spinner_->height()) / 2);
}

void StatusBanner::resizeEvent(QResizeEvent* event)
{
    QStatusBar::resizeEvent(event);
    placeSpinner();
}

void StatusBanner::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    // El fondo y el filete de arriba: los de la hoja de estilo del banner.
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_PanelStatusBar, &option, &painter, this);

    const QString message = currentMessage();
    if (message.isEmpty()) {
        return;
    }
    int left = kMargin + (spinner_->isVisible() ? spinner_->width() + kGap : 0);
    int right = width() - 12;
    // Los widgets permanentes (a la derecha) acotan el texto.
    for (const QObject* child : children()) {
        const auto* widget = qobject_cast<const QWidget*>(child);
        if (widget != nullptr && widget != spinner_ && widget->isVisible() && widget->x() > width() / 3) {
            right = std::min(right, widget->x() - 2);
        }
    }
    if (right <= left) {
        return;
    }
    QFont font = this->font();
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.setPen(Qt::white);
    const QRect area(left, 0, right - left, height());
    painter.drawText(area, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     QFontMetrics(font).elidedText(message, Qt::ElideRight, area.width()));
}

} // namespace nuf::app
