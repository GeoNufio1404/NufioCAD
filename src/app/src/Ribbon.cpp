#include "Ribbon.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <QResizeEvent>
#include <QShowEvent>

#include <algorithm>

namespace nuf::app {

namespace {

constexpr int kIconSize = 28;
constexpr int kButtonWidth = 50;

// Pestañas pintadas con la paleta actual (una hoja de estilos conservaba los
// colores del tema anterior al cambiar de tema).
class RibbonTabBar final : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    QSize tabSizeHint(int index) const override
    {
        const QSize base = QTabBar::tabSizeHint(index);
        return QSize(fontMetrics().horizontalAdvance(tabText(index)) + 32, base.height() + 4);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        painter.setFont(bold);
        for (int i = 0; i < count(); ++i) {
            if (!isTabVisible(i)) {
                continue;
            }
            const QRect rect = tabRect(i);
            const bool selected = i == currentIndex();
            painter.setPen(palette().color(QPalette::WindowText));
            painter.drawText(rect, Qt::AlignCenter, tabText(i));
            if (selected) {
                painter.fillRect(QRect(rect.left() + 6, rect.bottom() - 1, rect.width() - 12, 2),
                                 palette().color(QPalette::Highlight));
            }
        }
    }
};

} // namespace

Ribbon::Ribbon(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    tabs_ = new RibbonTabBar(this);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(false);
    tabs_->setFocusPolicy(Qt::NoFocus);

    host_ = new QWidget(this);
    new QVBoxLayout(host_);
    host_->layout()->setContentsMargins(0, 0, 0, 0);
    // Si la ventana es angosta, la cinta se desplaza en vez de ensanchar la ventana.
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(host_);
    // El fondo lo pinta la propia cinta con la paleta actual.
    host_->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    scroll->setFixedHeight(96);
    scroll_ = scroll;

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Plain);
    line->setForegroundRole(QPalette::Mid);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* tabRow = new QHBoxLayout();
    tabRow->setContentsMargins(0, 0, 8, 2);
    tabRow->addWidget(tabs_);
    tabRow->addStretch();
    trailingRow_ = tabRow;
    overflowMenu_ = new QMenu(this);
    overflow_ = new QToolButton(this);
    overflow_->setText(QStringLiteral("Más »"));
    overflow_->setToolTip(QStringLiteral("Más herramientas de esta pestaña"));
    overflow_->setPopupMode(QToolButton::InstantPopup);
    overflow_->setMenu(overflowMenu_);
    overflow_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    overflow_->setAutoRaise(true);
    overflow_->setFocusPolicy(Qt::NoFocus);
    overflow_->hide();
    tabRow->addWidget(overflow_);
    layout->addLayout(tabRow);
    layout->addWidget(line);
    layout->addWidget(scroll);

    // Solo la página de la pestaña actual cuenta para el ancho de la cinta.
    connect(tabs_, &QTabBar::currentChanged, this, [this](int current) {
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            const bool shown = static_cast<int>(i) == current;
            pages_[i].widget->setVisible(shown);
            if (pages_[i].trailing) {
                pages_[i].trailing->setVisible(shown);
            }
        }
        adapt();
    });
}

void Ribbon::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Button));
}

void Ribbon::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    adapt();
}

void Ribbon::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    adapt();
}

namespace {

// Lo que mide el grupo con los botones que lleva ahora.
int widthOf(const QWidget* widget)
{
    return widget->sizeHint().width();
}

} // namespace

void Ribbon::adapt()
{
    if (adapting_ || pages_.empty()) {
        return;
    }
    const int current = tabs_->currentIndex();
    if (current < 0 || current >= int(pages_.size())) {
        return;
    }
    adapting_ = true;
    Page& page = pages_[std::size_t(current)];
    // Ancho que le queda a los botones: el de la ventana menos los márgenes de la página y el botón «Más».
    const int margins = 16;
    const int available = std::max(0, width() - margins);

    const auto showAll = [&](bool icons) {
        for (Group& group : page.groups) {
            if (group.separator != nullptr) {
                group.separator->show();
            }
            group.widget->show();
            for (Entry& entry : group.entries) {
                entry.button->setToolButtonStyle(icons ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
                entry.button->setMinimumWidth(icons ? 34 : entry.textMinWidth);
                // Solo iconos: botones bajos, para que la cinta no crezca.
                entry.button->setFixedHeight(icons ? 40 : QWIDGETSIZE_MAX);
                if (!icons) {
                    entry.button->setMinimumHeight(0);
                    entry.button->setMaximumHeight(QWIDGETSIZE_MAX);
                }
            }
        }
    };
    const auto needed = [&](int firstHidden) {
        int total = 0;
        for (int i = 0; i < int(page.groups.size()) && i < firstHidden; ++i) {
            total += widthOf(page.groups[std::size_t(i)].widget) + (page.groups[std::size_t(i)].separator != nullptr ? 3 : 0);
        }
        return total;
    };
    const int count = int(page.groups.size());

    showAll(false);
    page.widget->layout()->activate();
    fit_ = Fit::Full;
    hiddenGroups_ = 0;
    if (needed(count) > available) {
        showAll(true);
        page.widget->layout()->activate();
        fit_ = Fit::IconsOnly;
    }
    if (fit_ == Fit::IconsOnly && needed(count) > available) {
        // Se recogen los últimos grupos hasta que el resto quepa con el botón «Más».
        const int reserve = overflow_->sizeHint().width() + 8;
        int visible = count;
        while (visible > 1 && needed(visible) > available - reserve) {
            --visible;
        }
        for (int i = visible; i < count; ++i) {
            Group& group = page.groups[std::size_t(i)];
            if (group.separator != nullptr) {
                group.separator->hide();
            }
            group.widget->hide();
        }
        hiddenGroups_ = count - visible;
        fit_ = Fit::Collapsed;
        fillOverflowMenu(page, visible);
    }
    overflow_->setVisible(hiddenGroups_ > 0);
    for (Page& each : pages_) {
        for (Group& group : each.groups) {
            if (group.label != nullptr) {
                group.label->setVisible(!compact_);
            }
        }
    }
    // El alto de la cinta: con nombres bajo los iconos 96; solo iconos 62; sin nombres de grupo, 18 menos.
    scroll_->setFixedHeight((fit_ == Fit::Full ? 96 : 62) - (compact_ ? 18 : 0));
    adapting_ = false;
}

void Ribbon::setCompact(bool compact)
{
    if (compact_ == compact) {
        return;
    }
    compact_ = compact;
    adapt();
}

void Ribbon::fillOverflowMenu(const Page& page, int firstHidden)
{
    overflowMenu_->clear();
    for (int i = firstHidden; i < int(page.groups.size()); ++i) {
        const Group& group = page.groups[std::size_t(i)];
        if (!group.title.isEmpty()) {
            overflowMenu_->addSection(group.title);
        }
        for (const Entry& entry : group.entries) {
            QToolButton* button = entry.button;
            QMenu* family = button->menu();
            if (family == nullptr) {
                if (QAction* action = button->defaultAction()) {
                    overflowMenu_->addAction(action);
                }
                continue;
            }
            // Una familia de variantes (o un botón de menú): un submenú con sus entradas.
            const QString name = button->defaultAction() != nullptr ? button->defaultAction()->text() : button->text();
            QMenu* sub = overflowMenu_->addMenu(button->icon(), name);
            for (QAction* action : family->actions()) {
                sub->addAction(action);
            }
        }
    }
}

int Ribbon::addTab(const QString& title)
{
    Page page;
    page.widget = new QWidget();
    page.layout = new QHBoxLayout(page.widget);
    page.layout->setContentsMargins(8, 4, 8, 2);
    page.layout->setSpacing(0);
    page.layout->addStretch();  // lo que va después del estiramiento queda a la derecha
    host_->layout()->addWidget(page.widget);
    page.widget->setVisible(pages_.empty());
    pages_.push_back(page);
    return tabs_->addTab(title);
}

void Ribbon::addGroup(int tab, const QString& title)
{
    Page& page = pages_[static_cast<std::size_t>(tab)];
    if (page.row) {
        auto* separator = new QFrame(page.widget);
        separator->setFrameShape(QFrame::VLine);
        separator->setFrameShadow(QFrame::Plain);
        separator->setForegroundRole(QPalette::Mid);
        page.layout->insertWidget(page.layout->count() - 1, separator);
    }
    auto* group = new QWidget(page.widget);
    Group record;
    record.widget = group;
    record.title = title;
    if (!page.groups.empty()) {
        record.separator = page.layout->itemAt(page.layout->count() - 2)->widget();  // la línea recién puesta
    }
    page.groups.push_back(record);
    auto* column = new QVBoxLayout(group);
    column->setContentsMargins(4, 0, 4, 0);
    column->setSpacing(1);
    auto* buttons = new QWidget(group);
    page.row = new QHBoxLayout(buttons);
    page.row->setContentsMargins(0, 0, 0, 0);
    page.row->setSpacing(2);
    auto* label = new QLabel(title, group);
    label->setAlignment(Qt::AlignHCenter);
    label->setForegroundRole(QPalette::PlaceholderText);
    QFont font = label->font();
    font.setPointSizeF(font.pointSizeF() * 0.85);
    label->setFont(font);
    column->addWidget(buttons, 1);
    column->addWidget(label);
    page.groups.back().label = label;
    page.layout->insertWidget(page.layout->count() - 1, group);
}

QToolButton* Ribbon::makeButton(QWidget* parent) const
{
    auto* button = new QToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setAutoRaise(true);
    button->setMinimumWidth(kButtonWidth);
    button->setFocusPolicy(Qt::NoFocus);
    QFont font = button->font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    button->setFont(font);
    return button;
}

void Ribbon::place(int tab, QWidget* button)
{
    Page& page = pages_[static_cast<std::size_t>(tab)];
    if (!page.row) {
        addGroup(tab, QString());
    }
    button->setParent(page.row->parentWidget());
    page.row->addWidget(button);
    // Que el texto no se corte: ancho mínimo = el que pide el botón.
    const int minimum = std::max(kButtonWidth, button->sizeHint().width());
    button->setMinimumWidth(minimum);
    if (!page.groups.empty()) {
        page.groups.back().entries.push_back({qobject_cast<QToolButton*>(button), minimum});
    }
}

QToolButton* Ribbon::addAction(int tab, QAction* action)
{
    QToolButton* button = makeButton(this);
    button->setDefaultAction(action);
    place(tab, button);
    return button;
}

QToolButton* Ribbon::addFamily(int tab, QMenu* menu, const std::vector<QAction*>& actions)
{
    if (!menu) {
        return addAction(tab, actions.front());
    }
    QToolButton* button = makeButton(this);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setMenu(menu);
    button->setDefaultAction(actions.front());
    for (QAction* action : actions) {
        connect(action, &QAction::triggered, button, [button, action] { button->setDefaultAction(action); });
    }
    place(tab, button);
    return button;
}

QToolButton* Ribbon::addMenu(int tab, const QString& text, const QIcon& icon, QMenu* menu)
{
    QToolButton* button = makeButton(this);
    button->setText(text);
    button->setIcon(icon);
    button->setToolTip(QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped()));
    button->setPopupMode(QToolButton::InstantPopup);
    button->setMenu(menu);
    place(tab, button);
    return button;
}

QToolButton* Ribbon::addTrailing(int tab, QAction* action)
{
    Page& page = pages_[static_cast<std::size_t>(tab)];
    auto* button = new QToolButton(this);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setDefaultAction(action);
    button->setFocusPolicy(Qt::NoFocus);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { background: #2E9D5B; color: white; border: 0; border-radius: 4px; padding: 4px 16px;"
        " font-weight: 600; }"
        "QToolButton:hover { background: #38B36A; }"
        "QToolButton:disabled { background: #2E9D5B55; color: #FFFFFF88; }"));
    button->setVisible(tabs_->currentIndex() == tab);
    trailingRow_->addWidget(button);
    page.trailing = button;
    return button;
}

void Ribbon::setTabVisible(int tab, bool visible)
{
    tabs_->setTabVisible(tab, visible);
    // El QTabBar no recalcula su ancho al ocultar/mostrar pestañas.
    tabs_->setFixedWidth(tabs_->sizeHint().width());
}

void Ribbon::setCurrentTab(int tab)
{
    tabs_->setCurrentIndex(tab);
}

} // namespace nuf::app
