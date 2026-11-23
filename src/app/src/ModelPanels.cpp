#include "ModelPanels.hpp"
#include "Units.hpp"
#include "Icons.hpp"
#include "Theme.hpp"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace nuf::app {

namespace {

constexpr int kMargin = 10;
constexpr int kChipWidth = 76;
constexpr int kChipHeight = 54;
constexpr int kSpacing = 6;
constexpr int kStep = kChipWidth + kSpacing;
constexpr int kMarkerGap = 14;  // hueco del marcador entre fichas
constexpr int kTop = 6;

QString glyph(document::FeatureKind kind)
{
    switch (kind) {
    case document::FeatureKind::Sketch:
        return QStringLiteral("✎");
    case document::FeatureKind::Extrude:
        return QStringLiteral("⬆");
    case document::FeatureKind::Revolve:
        return QStringLiteral("⟳");
    case document::FeatureKind::Import:
        return QStringLiteral("⇩");
    case document::FeatureKind::Fillet:
        return QStringLiteral("◜");
    case document::FeatureKind::Chamfer:
        return QStringLiteral("◿");
    case document::FeatureKind::Shell:
        return QStringLiteral("▢");
    case document::FeatureKind::Hole:
        return QStringLiteral("◎");
    case document::FeatureKind::Mirror:
        return QStringLiteral("⇋");
    case document::FeatureKind::Split:
        return QStringLiteral("⫽");
    case document::FeatureKind::Pattern:
        return QStringLiteral("▦");
    case document::FeatureKind::ConstructionPlane:
        return QStringLiteral("▱");
    case document::FeatureKind::ConstructionAxis:
        return QStringLiteral("╱");
    case document::FeatureKind::Box:
        return QStringLiteral("▭");
    case document::FeatureKind::Cylinder:
        return QStringLiteral("◯");
    case document::FeatureKind::Gear:
        return QStringLiteral("⚙");
    case document::FeatureKind::Thread:
        return QStringLiteral("⟲");
    case document::FeatureKind::Sweep:
        return QStringLiteral("⤳");
    case document::FeatureKind::Loft:
        return QStringLiteral("⬯");
    case document::FeatureKind::Move:
        return QStringLiteral("✥");
    case document::FeatureKind::Combine:
        return QStringLiteral("⊕");
    case document::FeatureKind::Joint:
        return QStringLiteral("⚓");
    case document::FeatureKind::Align:
        return QStringLiteral("⊞");
    case document::FeatureKind::MotionLink:
        return QStringLiteral("⛓");
    }
    return QStringLiteral("?");
}

QString statusText(const document::FeatureStatus& status)
{
    switch (status.health) {
    case document::Health::Ok:
        return {};
    case document::Health::Warning:
        return QStringLiteral("Aviso: %1").arg(Units::instance().localize(QString::fromStdString(status.message)));
    case document::Health::Error:
        return QStringLiteral("Error: %1").arg(Units::instance().localize(QString::fromStdString(status.message)));
    case document::Health::Suppressed:
        return QStringLiteral("Suprimida");
    case document::Health::RolledBack:
        return QStringLiteral("Después del marcador (no se calcula)");
    }
    return {};
}

enum Role { KindRole = Qt::UserRole, KeyRole };
enum ItemKind { OriginItem = 1, BodyItem, SketchItem, GroundItem, PlaneItem, ComponentItem };

} // namespace

// --- Timeline ------------------------------------------------------------------

TimelineWidget::TimelineWidget(const document::Document& document, QWidget* parent)
    : QWidget(parent)
    , document_(document)
{
    setMouseTracking(true);
    setMinimumHeight(kChipHeight + 2 * kTop);
}

QSize TimelineWidget::sizeHint() const
{
    const int n = int(document_.featureCount());
    return {2 * kMargin + n * kStep + kMarkerGap, kChipHeight + 2 * kTop};
}

QSize TimelineWidget::minimumSizeHint() const
{
    return {kMargin * 2 + kMarkerGap, kChipHeight + 2 * kTop};
}

void TimelineWidget::setEditing(std::optional<document::FeatureId> id)
{
    editing_ = id;
    update();
}

QRect TimelineWidget::chipRect(std::size_t index) const
{
    const int x = kMargin + int(index) * kStep + (index >= document_.marker() ? kMarkerGap : 0);
    return {x, kTop, kChipWidth, kChipHeight};
}

int TimelineWidget::markerX(std::size_t marker) const
{
    return kMargin + int(marker) * kStep + kMarkerGap / 2 - kSpacing / 2;
}

std::optional<std::size_t> TimelineWidget::chipAt(const QPoint& pos) const
{
    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        if (chipRect(i).contains(pos)) {
            return i;
        }
    }
    return std::nullopt;
}

int TimelineWidget::boundaryX(std::size_t gap) const
{
    const std::size_t n = document_.featureCount();
    if (gap == document_.marker()) {
        return markerX(gap);
    }
    if (gap < n) {
        return chipRect(gap).left() - kSpacing / 2;
    }
    return chipRect(n - 1).right() + kSpacing / 2 + 1;
}

std::size_t TimelineWidget::gapAt(int x) const
{
    std::size_t best = 0;
    for (std::size_t g = 0; g <= document_.featureCount(); ++g) {
        if (std::abs(boundaryX(g) - x) < std::abs(boundaryX(best) - x)) {
            best = g;
        }
    }
    return best;
}

void TimelineWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QColor accent(0x1F, 0x6F, 0xEB);
    const QColor marker(0xF2, 0xA3, 0x4B);

    if (document_.featureCount() == 0) {
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(kMargin, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("El historial está vacío: crea un sketch para empezar."));
        return;
    }

    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        const document::Feature& f = document_.feature(i);
        const document::FeatureStatus& status = document_.status(f.id());
        const QRect r = chipRect(i);
        QColor fill = pal.color(QPalette::Button);
        QColor border = pal.color(QPalette::Mid);
        if (status.health == document::Health::Error) {
            fill = QColor(0xE5, 0x48, 0x4D, 70);
            border = QColor(0xE5, 0x48, 0x4D);
        } else if (status.health == document::Health::Warning) {
            fill = QColor(0xF2, 0xA3, 0x4B, 60);
            border = QColor(0xF2, 0xA3, 0x4B);
        }
        if (editing_ == f.id()) {
            border = accent;
        }
        const bool faded = status.health == document::Health::RolledBack
                           || status.health == document::Health::Suppressed;
        p.setOpacity(faded ? 0.45 : 1.0);
        if (draggedChip_ == i && dropGap_) {
            p.setOpacity(0.3);
        }
        p.setPen(QPen(border, editing_ == f.id() ? 2.0 : 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);

        QFont big = font();
        big.setPointSizeF(big.pointSizeF() * 1.4);
        p.setFont(big);
        p.setPen(pal.color(QPalette::ButtonText));
        p.drawText(QRect(r.x(), r.y() + 2, r.width(), 26), Qt::AlignCenter, glyph(f.kind()));

        QFont small = font();
        small.setPointSizeF(small.pointSizeF() * 0.85);
        small.setStrikeOut(status.health == document::Health::Suppressed);
        p.setFont(small);
        const QFontMetrics fm(small);
        const QString name = fm.elidedText(QString::fromStdString(f.name()), Qt::ElideRight, r.width() - 6);
        p.drawText(QRect(r.x() + 3, r.y() + 28, r.width() - 6, 22), Qt::AlignCenter, name);
        p.setOpacity(1.0);
        // Insignia «!» en la esquina (el motivo, en el tooltip).
        if (status.health == document::Health::Error || status.health == document::Health::Warning) {
            const QRectF badge(r.right() - 15, r.top() + 3, 12, 12);
            p.setPen(Qt::NoPen);
            p.setBrush(border);
            p.drawEllipse(badge);
            QFont tiny = font();
            tiny.setBold(true);
            tiny.setPointSizeF(tiny.pointSizeF() * 0.7);
            p.setFont(tiny);
            p.setPen(Qt::white);
            p.drawText(badge, Qt::AlignCenter, QStringLiteral("!"));
        }
    }

    // Marcador (y, al arrastrar, dónde caería).
    const auto drawBar = [&](int x, const QColor& color, qreal width) {
        p.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(x, kTop - 2, x, kTop + kChipHeight + 2);
        QPainterPath tab;
        tab.addRoundedRect(QRectF(x - 5, kTop - 4, 10, 8), 2, 2);
        p.fillPath(tab, color);
    };
    drawBar(markerX(document_.marker()), marker, 3.0);
    if (dropGap_ && (draggingMarker_ || draggedChip_)) {
        drawBar(boundaryX(*dropGap_), draggingMarker_ ? marker : accent, 2.0);
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    pressPos_ = event->pos();
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (std::abs(event->pos().x() - markerX(document_.marker())) <= 6) {
        draggingMarker_ = true;
        dropGap_ = document_.marker();
        update();
        return;
    }
    if (const auto chip = chipAt(event->pos())) {
        draggedChip_ = *chip;
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingMarker_) {
        dropGap_ = gapAt(event->pos().x());
        update();
        return;
    }
    if (draggedChip_ && (event->pos() - pressPos_).manhattanLength() > 8) {
        dropGap_ = gapAt(event->pos().x());
        setCursor(Qt::ClosedHandCursor);
        update();
        return;
    }
    const bool overMarker = std::abs(event->pos().x() - markerX(document_.marker())) <= 6;
    setCursor(overMarker ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    if (draggingMarker_ && dropGap_ && *dropGap_ != document_.marker()) {
        emit markerMoved(*dropGap_);
    } else if (draggedChip_ && dropGap_) {
        // Hueco contado con la ficha → posición sin ella.
        std::size_t to = *dropGap_;
        if (to > *draggedChip_) {
            --to;
        }
        if (to != *draggedChip_) {
            emit featureMoved(document_.feature(*draggedChip_).id(), to);
        }
    }
    draggingMarker_ = false;
    draggedChip_.reset();
    dropGap_.reset();
    unsetCursor();
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (const auto chip = chipAt(event->pos())) {
        emit featureActivated(document_.feature(*chip).id());
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (const auto chip = chipAt(event->pos())) {
        emit featureMenuRequested(document_.feature(*chip).id(), event->globalPos());
    }
}

bool TimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const auto chip = chipAt(help->pos())) {
            const document::Feature& f = document_.feature(*chip);
            const QString status = statusText(document_.status(f.id()));
            QToolTip::showText(help->globalPos(), QString::fromStdString(f.name())
                                                      + (status.isEmpty() ? QString() : QStringLiteral("\n") + status));
        } else if (std::abs(help->pos().x() - markerX(document_.marker())) <= 6) {
            QToolTip::showText(help->globalPos(), QStringLiteral("Marcador: arrástralo para volver atrás en el historial"));
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}

// --- Navegador -----------------------------------------------------------------

NavigatorWidget::NavigatorWidget(QWidget* parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setSelectionMode(QAbstractItemView::ExtendedSelection);  // varios cuerpos o componentes a la vez (Duplicar, Fijar…)
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item) {
        if (refreshing_) {
            return;
        }
        // Se avisa fuera de la señal del propio árbol: quien recibe rehace el árbol entero
        // (refresh() lo vacía) y no puede borrar el elemento mientras Qt aún lo está usando.
        // Varios clics seguidos solo encolan varios avisos.
        const bool visible = item->checkState(0) == Qt::Checked;
        const int kind = item->data(0, KindRole).toInt();
        const QVariant key = item->data(0, KeyRole);
        QTimer::singleShot(0, this, [this, visible, kind, key] {
            switch (kind) {
            case OriginItem:
                emit originVisibilityChanged(visible);
                break;
            case GroundItem:
                emit groundVisibilityChanged(visible);
                break;
            case BodyItem:
                emit bodyVisibilityChanged(key.toString().toStdString(), visible);
                break;
            case SketchItem:
                emit sketchVisibilityChanged(key.toULongLong(), visible);
                break;
            case PlaneItem:
                emit planeVisibilityChanged(key.toULongLong(), visible);
                break;
            case ComponentItem:
                emit componentVisibilityChanged(key.toString().toStdString(), visible);
                break;
            default:
                break;
            }
        });
    });
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        if (item->data(0, KindRole).toInt() == SketchItem) {
            const auto id = item->data(0, KeyRole).toULongLong();
            QTimer::singleShot(0, this, [this, id] { emit sketchActivated(id); });
        }
    });
}

void NavigatorWidget::refresh(const document::Document& document, const document::ModelState& state,
                              std::size_t upTo, const std::set<std::string>& hiddenBodies,
                              const std::set<document::FeatureId>& visibleSketches, bool originVisible,
                              bool groundVisible, const std::set<document::FeatureId>& hiddenPlanes)
{
    refreshing_ = true;
    clear();
    fixed_.clear();
    inComponent_.clear();
    const auto checkable = [](QTreeWidgetItem* item, bool on, int kind, const QVariant& key) {
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
        item->setData(0, KindRole, kind);
        item->setData(0, KeyRole, key);
    };

    // La raíz es el propio diseño, con su nombre (U5), como en Fusion 360.
    const QString title = QString::fromStdString(document.name()).isEmpty() ? QStringLiteral("Sin título")
                                                                            : QString::fromStdString(document.name());
    auto* root = new QTreeWidgetItem(this, {title});
    root->setData(0, KindRole, -1);
    QFont rootFont = root->font(0);
    rootFont.setBold(true);
    root->setFont(0, rootFont);
    auto* origin = new QTreeWidgetItem(root, {QStringLiteral("Origen")});
    checkable(origin, originVisible, OriginItem, {});
    auto* ground = new QTreeWidgetItem(root, {QStringLiteral("Rejilla del suelo")});
    checkable(ground, groundVisible, GroundItem, {});

    QTreeWidgetItem* planes = nullptr;
    for (std::size_t i = 0; i < std::min(upTo, document.featureCount()); ++i) {
        const document::Feature& f = document.feature(i);
        if (f.kind() != document::FeatureKind::ConstructionPlane || !state.planes.contains(f.id())) {
            continue;
        }
        if (planes == nullptr) {
            planes = new QTreeWidgetItem(root, {QStringLiteral("Planos de construcción")});
        }
        auto* item = new QTreeWidgetItem(planes, {QString::fromStdString(f.name())});
        checkable(item, !hiddenPlanes.contains(f.id()), PlaneItem, QVariant::fromValue<qulonglong>(f.id()));
    }

    QTreeWidgetItem* axes = nullptr;
    for (std::size_t i = 0; i < std::min(upTo, document.featureCount()); ++i) {
        const document::Feature& f = document.feature(i);
        if (f.kind() != document::FeatureKind::ConstructionAxis || !state.axes.contains(f.id())) {
            continue;
        }
        if (axes == nullptr) {
            axes = new QTreeWidgetItem(root, {QStringLiteral("Ejes de construcción")});
        }
        auto* item = new QTreeWidgetItem(axes, {QString::fromStdString(f.name())});
        checkable(item, !hiddenPlanes.contains(f.id()), PlaneItem, QVariant::fromValue<qulonglong>(f.id()));
    }

    auto* bodies = new QTreeWidgetItem(root, {QStringLiteral("Cuerpos")});
    std::map<std::string, QTreeWidgetItem*> components;
    for (const document::Body& body : state.bodies) {
        QTreeWidgetItem* parent = bodies;
        const std::string component = document.componentOf(body.key);
        if (!component.empty()) {
            auto& node = components[component];
            if (node == nullptr) {
                node = new QTreeWidgetItem(root, {QString::fromStdString(component)});
                node->setIcon(0, QIcon());
            }
            parent = node;
        }
        auto* item = new QTreeWidgetItem(parent, {QString::fromStdString(body.name)});
        checkable(item, !hiddenBodies.contains(body.key), BodyItem, QString::fromStdString(body.key));
        inComponent_[body.key] = !component.empty();
        if (document.isFixed(body.key)) {
            // Un candado marca el cuerpo fijo: no se mueve con Alinear, Unión ni arrastrando.
            fixed_.insert(body.key);
            item->setIcon(0, uiIcon(QStringLiteral("lock")));
            item->setToolTip(0, QStringLiteral("Fijo: no se mueve (clic derecho › Soltar)"));
        }
    }
    // Con todos los cuerpos dentro de componentes la carpeta «Cuerpos» quedaría vacía: se quita (sin cuerpos sigue ahí).
    if (!state.bodies.empty() && bodies->childCount() == 0) {
        delete bodies;
        bodies = nullptr;
    }
    // Cada componente se muestra u oculta de una vez (marcado si todos sus cuerpos se ven).
    for (auto& [name, node] : components) {
        bool allShown = true;
        for (int i = 0; i < node->childCount(); ++i) {
            allShown = allShown && node->child(i)->checkState(0) == Qt::Checked;
        }
        checkable(node, allShown, ComponentItem, QString::fromStdString(name));
    }

    auto* sketches = new QTreeWidgetItem(root, {QStringLiteral("Sketches")});
    for (std::size_t i = 0; i < std::min(upTo, document.featureCount()); ++i) {
        const document::Feature& f = document.feature(i);
        if (f.kind() != document::FeatureKind::Sketch) {
            continue;
        }
        auto* item = new QTreeWidgetItem(sketches, {QString::fromStdString(f.name())});
        checkable(item, visibleSketches.contains(f.id()), SketchItem, QVariant::fromValue<qulonglong>(f.id()));
        const document::FeatureStatus& status = document.status(f.id());
        if (status.health == document::Health::Warning || status.health == document::Health::Error) {
            item->setToolTip(0, Units::instance().localize(QString::fromStdString(status.message)));
            item->setForeground(0, QColor(toneColor(Tone::Warning)));
        }
    }
    expandAll();
    refreshing_ = false;
}

std::vector<std::string> NavigatorWidget::selectedBodies() const
{
    std::vector<std::string> keys;
    const auto add = [&](const std::string& key) {
        if (std::ranges::find(keys, key) == keys.end()) {
            keys.push_back(key);
        }
    };
    for (QTreeWidgetItem* item : selectedItems()) {
        const int kind = item->data(0, KindRole).toInt();
        if (kind == BodyItem) {
            add(item->data(0, KeyRole).toString().toStdString());
        } else if (kind == ComponentItem) {
            for (int i = 0; i < item->childCount(); ++i) {
                add(item->child(i)->data(0, KeyRole).toString().toStdString());
            }
        }
    }
    return keys;
}

void NavigatorWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QTreeWidgetItem* under = itemAt(event->pos());
    if (under == nullptr) {
        return;
    }
    const int kind = under->data(0, KindRole).toInt();
    if (kind != BodyItem && kind != ComponentItem) {
        return;
    }
    // Un clic derecho sobre algo que no está elegido lo elige solo (lo que se espera de un menú contextual).
    if (!under->isSelected()) {
        clearSelection();
        under->setSelected(true);
    }
    const std::vector<std::string> keys = selectedBodies();
    if (keys.empty()) {
        return;
    }
    const bool allFixed = std::ranges::all_of(keys, [&](const std::string& key) { return fixed_.contains(key); });
    const bool anyGrouped = std::ranges::any_of(keys, [&](const std::string& key) {
        const auto it = inComponent_.find(key);
        return it != inComponent_.end() && it->second;
    });
    QMenu menu(this);
    QAction* duplicate = menu.addAction(QStringLiteral("Duplicar	Ctrl+D"));
    QAction* move = menu.addAction(QStringLiteral("Mover…"));
    menu.addSeparator();
    QAction* fix = menu.addAction(allFixed ? QStringLiteral("Soltar (dejar de fijar)") : QStringLiteral("Fijar"));
    QAction* group = menu.addAction(QStringLiteral("Crear componente con la selección…"));
    QAction* ungroup = anyGrouped ? menu.addAction(QStringLiteral("Sacar del componente")) : nullptr;
    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == nullptr) {
        return;
    }
    if (chosen == duplicate) {
        emit duplicateRequested(keys);
    } else if (chosen == move) {
        emit moveRequested(keys);
    } else if (chosen == fix) {
        emit fixRequested(keys, !allFixed);
    } else if (chosen == group) {
        emit componentRequested(keys);
    } else if (ungroup != nullptr && chosen == ungroup) {
        emit ungroupRequested(keys);
    }
}

} // namespace nuf::app
