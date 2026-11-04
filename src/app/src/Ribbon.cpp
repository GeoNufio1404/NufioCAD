#include "Ribbon.hpp"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScrollArea>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace nuf::app {

namespace {

constexpr int kIconSize = 28;
constexpr int kButtonWidth = 50;

} // namespace

Ribbon::Ribbon(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    tabs_ = new QTabBar(this);
    tabs_->setDrawBase(false);
    tabs_->setExpanding(false);
    tabs_->setUsesScrollButtons(false);
    tabs_->setFocusPolicy(Qt::NoFocus);
    tabs_->setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 5px 16px; border: 0; border-bottom: 2px solid transparent; font-weight: 600; }"
        "QTabBar::tab:selected { border-bottom-color: palette(highlight); }"
        "QTabBar::tab:!selected { color: palette(window-text); }"));

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
    scroll->setFixedHeight(96);

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
    });
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
    button->setMinimumWidth(std::max(kButtonWidth, button->sizeHint().width()));
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
