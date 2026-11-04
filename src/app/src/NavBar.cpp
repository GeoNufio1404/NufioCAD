#include "NavBar.hpp"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>

namespace nuf::app {

namespace {

constexpr int kMargin = 14;

} // namespace

NavBar::NavBar(QWidget* viewport, const std::vector<QAction*>& actions) : QWidget(viewport)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(2);
    for (QAction* action : actions) {
        auto* button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(22, 22));
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
    }
    viewport->installEventFilter(this);
    reposition();
    show();
}

bool NavBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        reposition();
    }
    return QWidget::eventFilter(watched, event);
}

void NavBar::reposition()
{
    adjustSize();
    move((parentWidget()->width() - width()) / 2, parentWidget()->height() - height() - kMargin);
    raise();
}

void NavBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor fill = palette().color(QPalette::Window);
    fill.setAlpha(225);
    painter.setBrush(fill);
    painter.setPen(palette().color(QPalette::Mid));
    const QRectF outline = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.drawRoundedRect(outline, height() / 2.0, height() / 2.0);
}

} // namespace nuf::app
