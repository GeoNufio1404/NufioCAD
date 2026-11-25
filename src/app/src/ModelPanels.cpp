#include "ModelPanels.hpp"
#include "Units.hpp"
#include "Icons.hpp"
#include "Theme.hpp"

#include <QAbstractScrollArea>
#include <QContextMenuEvent>
#include <QCursor>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace nuf::app {

namespace {

constexpr int kMargin = 10;
constexpr int kChipWidth = 76;
constexpr int kChipHeight = 54;
constexpr int kSpacing = 6;
constexpr int kStep = kChipWidth + kSpacing;
constexpr int kMarkerGap = 14;  // hueco del marcador entre fichas
constexpr int kTop = 6;

constexpr int kIconSize = 24;

// El mismo icono que la herramienta que crea la operación (resources/icons/ui).
const char* iconName(document::FeatureKind kind)
{
    switch (kind) {
    case document::FeatureKind::Sketch: return "create-sketch";
    case document::FeatureKind::Extrude: return "extrude";
    case document::FeatureKind::Revolve: return "revolve";
    case document::FeatureKind::Import: return "file-open";
    case document::FeatureKind::Fillet: return "fillet";
    case document::FeatureKind::Chamfer: return "chamfer";
    case document::FeatureKind::Shell: return "shell";
    case document::FeatureKind::Hole: return "hole";
    case document::FeatureKind::Mirror: return "mirror-solid";
    case document::FeatureKind::Split: return "section-solid";
    case document::FeatureKind::Pattern: return "pattern-solid";
    case document::FeatureKind::ConstructionPlane: return "plane-solid";
    case document::FeatureKind::ConstructionAxis: return "axis-solid";
    case document::FeatureKind::Box: return "box-solid";
    case document::FeatureKind::Cylinder: return "cylinder-solid";
    case document::FeatureKind::Gear: return "gear-solid";
    case document::FeatureKind::Thread: return "thread-solid";
    case document::FeatureKind::Sweep: return "sweep";
    case document::FeatureKind::Loft: return "loft";
    case document::FeatureKind::Move: return "move";
    case document::FeatureKind::Combine: return "combine-solid";
    case document::FeatureKind::Joint: return "joint-solid";
    case document::FeatureKind::Align: return "align-solid";
    case document::FeatureKind::MotionLink: return "link-solid";
    }
    return "create-sketch";
}

// Los iconos se cargan una vez por tipo (el motor los repinta con los colores del tema vigente).
const QIcon& iconFor(document::FeatureKind kind)
{
    static std::map<document::FeatureKind, QIcon> cache;
    auto it = cache.find(kind);
    if (it == cache.end()) {
        it = cache.emplace(kind, uiIcon(QString::fromLatin1(iconName(kind)))).first;
    }
    return it->second;
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
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(kChipHeight + 2 * kTop);
    autoScroll_ = new QTimer(this);
    autoScroll_->setInterval(30);
    connect(autoScroll_, &QTimer::timeout, this, [this] { autoScrollStep(); });
}

QSize TimelineWidget::sizeHint() const
{
    const int n = int(document_.featureCount());
    return {2 * kMargin + n * kStep + kMarkerGap, kChipHeight + 2 * kTop};
}

QSize TimelineWidget::minimumSizeHint() const
{
    // Con el ancho completo como mínimo, el área de desplazamiento que lo contiene saca su barra horizontal en vez de
    // aplastar o esconder las fichas de la derecha.
    return {sizeHint().width(), kChipHeight + 2 * kTop};
}

void TimelineWidget::setEditing(std::optional<document::FeatureId> id)
{
    editing_ = id;
    if (id) {
        if (const auto index = document_.indexOf(*id)) {
            revealRange(chipRect(*index).left() - kMargin, chipRect(*index).right() + kMargin);
        }
    }
    update();
}

QScrollBar* TimelineWidget::scrollBar() const
{
    auto* area = qobject_cast<QAbstractScrollArea*>(parentWidget() != nullptr ? parentWidget()->parentWidget() : nullptr);
    return area != nullptr ? area->horizontalScrollBar() : nullptr;
}

int TimelineWidget::viewportWidth() const
{
    return parentWidget() != nullptr ? parentWidget()->width() : width();
}

void TimelineWidget::revealRange(int left, int right)
{
    QScrollBar* bar = scrollBar();
    if (bar == nullptr) {
        return;
    }
    const int view = viewportWidth();
    if (left < bar->value()) {
        bar->setValue(left);
    } else if (right > bar->value() + view) {
        bar->setValue(right - view);
    }
}

void TimelineWidget::historyChanged()
{
    // Lo que se borró o ya no está sale de la elección.
    for (auto it = selected_.begin(); it != selected_.end();) {
        it = document_.indexOf(*it) ? std::next(it) : selected_.erase(it);
    }
    if (anchor_ && !document_.indexOf(*anchor_)) {
        anchor_.reset();
    }
    if (current_ && !document_.indexOf(*current_)) {
        current_.reset();
    }
    const std::size_t n = document_.featureCount();
    if (n != lastCount_) {
        const bool grew = n > lastCount_;
        lastCount_ = n;
        if (grew) {
            // Con la barra ya ajustada al ancho nuevo, deja a la vista el marcador (donde entra lo recién hecho).
            QTimer::singleShot(0, this, [this] {
                const int x = markerX(document_.marker());
                revealRange(x - kStep, x + kStep);
            });
        }
    }
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
    // Las fichas están en orden y a paso fijo: se calcula la candidata, sin recorrerlas todas.
    const std::size_t n = document_.featureCount();
    if (n == 0) {
        return std::nullopt;
    }
    const int guess = std::clamp((pos.x() - kMargin) / kStep, 0, int(n) - 1);
    for (int i = std::max(0, guess - 1); i <= std::min(int(n) - 1, guess + 1); ++i) {
        if (chipRect(std::size_t(i)).contains(pos)) {
            return std::size_t(i);
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

bool TimelineWidget::isSelected(std::size_t index) const
{
    return !selected_.empty() && selected_.contains(document_.feature(index).id());
}

std::vector<std::size_t> TimelineWidget::selectedIndices() const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < document_.featureCount() && out.size() < selected_.size(); ++i) {
        if (isSelected(i)) {
            out.push_back(i);
        }
    }
    return out;
}

std::optional<std::pair<std::size_t, std::size_t>> TimelineWidget::selectionRange() const
{
    const std::vector<std::size_t> indices = selectedIndices();
    if (indices.empty() || indices.back() - indices.front() + 1 != indices.size()) {
        return std::nullopt;
    }
    return std::pair{indices.front(), indices.back()};
}

std::vector<document::FeatureId> TimelineWidget::selection() const
{
    std::vector<document::FeatureId> ids;
    for (const std::size_t i : selectedIndices()) {
        ids.push_back(document_.feature(i).id());
    }
    return ids;
}

void TimelineWidget::select(const std::vector<document::FeatureId>& ids)
{
    selected_.clear();
    for (const document::FeatureId id : ids) {
        if (document_.indexOf(id)) {
            selected_.insert(id);
        }
    }
    anchor_ = ids.empty() ? std::nullopt : std::optional<document::FeatureId>(ids.front());
    current_ = ids.empty() ? std::nullopt : std::optional<document::FeatureId>(ids.back());
    update();
}

void TimelineWidget::clearSelection()
{
    selected_.clear();
    anchor_.reset();
    current_.reset();
    update();
}

void TimelineWidget::selectRange(std::size_t from, std::size_t to)
{
    if (from > to) {
        std::swap(from, to);
    }
    selected_.clear();
    for (std::size_t i = from; i <= to && i < document_.featureCount(); ++i) {
        selected_.insert(document_.feature(i).id());
    }
}

void TimelineWidget::toggleContiguous(std::size_t index)
{
    const auto range = selectionRange();
    if (!range) {
        selectRange(index, index);
        return;
    }
    const auto [lo, hi] = *range;
    if (index + 1 == lo) {
        selectRange(index, hi);
    } else if (index == hi + 1) {
        selectRange(lo, index);
    } else if (index == lo && hi > lo) {
        selectRange(lo + 1, hi);
    } else if (index == hi && hi > lo) {
        selectRange(lo, hi - 1);
    } else {
        selectRange(index, index);  // no es contigua: solo se admiten fichas seguidas
    }
}

void TimelineWidget::setCurrent(std::size_t index)
{
    current_ = document_.feature(index).id();
    revealRange(chipRect(index).left() - kMargin, chipRect(index).right() + kMargin);
}

void TimelineWidget::emitBlockMove(std::size_t gap)
{
    const auto range = selectionRange();
    if (!range) {
        return;
    }
    const auto [lo, hi] = *range;
    if (gap >= lo && gap <= hi + 1) {
        return;  // caería donde ya está
    }
    emit blockMoved(selection(), gap);
}

void TimelineWidget::autoScrollStep()
{
    QScrollBar* bar = scrollBar();
    if ((!draggingMarker_ && !draggedChip_) || bar == nullptr) {
        autoScroll_->stop();
        return;
    }
    // Al arrastrar cerca de un borde de lo visible, la timeline se desplaza sola.
    const int x = mapFromGlobal(QCursor::pos()).x();
    const int left = bar->value();
    const int right = left + viewportWidth();
    constexpr int kEdge = 36;
    if (x < left + kEdge) {
        bar->setValue(bar->value() - std::max(4, (left + kEdge - x) / 3));
    } else if (x > right - kEdge) {
        bar->setValue(bar->value() + std::max(4, (x - (right - kEdge)) / 3));
    } else {
        return;
    }
    if (dropGap_) {
        dropGap_ = gapAt(x);
        update();
    }
}

void TimelineWidget::paintEvent(QPaintEvent* event)
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

    // Solo las fichas que caen en la zona a repintar (con cientos de operaciones, el resto ni se toca).
    const std::size_t n = document_.featureCount();
    const QRect dirty = event->rect();
    const std::size_t first = std::size_t(std::max(0, (dirty.left() - kMargin) / kStep - 1));
    const std::size_t last = std::min(n, std::size_t(std::max(0, (dirty.right() - kMargin) / kStep + 2)));
    const bool dragging = dropGap_ && draggedChip_;
    QFont small = font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    QFont tiny = font();
    tiny.setPointSizeF(tiny.pointSizeF() * 0.7);
    for (std::size_t i = first; i < last; ++i) {
        const document::Feature& f = document_.feature(i);
        const document::FeatureStatus& status = document_.status(f.id());
        const QRect r = chipRect(i);
        if (!r.intersects(dirty)) {
            continue;
        }
        const bool chosen = isSelected(i);
        QColor fill = pal.color(QPalette::Button);
        QColor border = pal.color(QPalette::Mid);
        if (status.health == document::Health::Error) {
            fill = QColor(0xE5, 0x48, 0x4D, 70);
            border = QColor(0xE5, 0x48, 0x4D);
        } else if (status.health == document::Health::Warning) {
            fill = QColor(0xF2, 0xA3, 0x4B, 60);
            border = QColor(0xF2, 0xA3, 0x4B);
        }
        if (chosen) {
            fill = pal.color(QPalette::Highlight);
            fill.setAlpha(90);
            border = pal.color(QPalette::Highlight);
        }
        if (editing_ == f.id()) {
            border = accent;
        }
        const bool faded = status.health == document::Health::RolledBack
                           || status.health == document::Health::Suppressed;
        p.setOpacity(faded ? 0.45 : 1.0);
        if (dragging && (chosen || draggedChip_ == i)) {
            p.setOpacity(0.3);
        }
        p.setPen(QPen(border, editing_ == f.id() || chosen ? 2.0 : 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);

        // El mismo dibujo que la herramienta que la creó.
        iconFor(f.kind()).paint(&p, QRect(r.center().x() - kIconSize / 2, r.y() + 5, kIconSize, kIconSize),
                                Qt::AlignCenter, faded ? QIcon::Disabled : QIcon::Normal);

        QFont named = small;
        named.setStrikeOut(status.health == document::Health::Suppressed);
        p.setFont(named);
        p.setPen(pal.color(QPalette::ButtonText));
        const QFontMetrics fm(named);
        const QString name = fm.elidedText(QString::fromStdString(f.name()), Qt::ElideRight, r.width() - 6);
        p.drawText(QRect(r.x() + 3, r.y() + 5 + kIconSize + 1, r.width() - 6, 18), Qt::AlignCenter, name);
        // El número de orden (arriba a la izquierda): ayuda a ubicarse cuando hay muchas.
        p.setFont(tiny);
        p.setPen(pal.color(QPalette::PlaceholderText));
        p.drawText(QRect(r.x() + 4, r.y() + 2, 24, 12), Qt::AlignLeft | Qt::AlignTop, QString::number(i + 1));
        p.setOpacity(1.0);
        // Insignia «!» en la esquina (el motivo, en el tooltip).
        if (status.health == document::Health::Error || status.health == document::Health::Warning) {
            const QRectF badge(r.right() - 15, r.top() + 3, 12, 12);
            p.setPen(Qt::NoPen);
            p.setBrush(border);
            p.drawEllipse(badge);
            QFont bold = font();
            bold.setBold(true);
            bold.setPointSizeF(bold.pointSizeF() * 0.7);
            p.setFont(bold);
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
    collapseOnRelease_ = false;
    setFocus(Qt::MouseFocusReason);
    const auto chip = chipAt(event->pos());
    if (event->button() == Qt::RightButton) {
        // El clic derecho sobre una ficha no elegida la elige sola; sobre una de varias elegidas conserva la elección.
        if (chip && !isSelected(*chip)) {
            selectRange(*chip, *chip);
            anchor_ = current_ = document_.feature(*chip).id();
            update();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (std::abs(event->pos().x() - markerX(document_.marker())) <= 6) {
        draggingMarker_ = true;
        dropGap_ = document_.marker();
        autoScroll_->start();
        update();
        return;
    }
    if (!chip) {
        clearSelection();  // clic en el vacío
        return;
    }
    const Qt::KeyboardModifiers mods = event->modifiers();
    const std::optional<std::size_t> anchorIndex = anchor_ ? document_.indexOf(*anchor_) : std::nullopt;
    if ((mods & Qt::ShiftModifier) && anchorIndex) {
        selectRange(*anchorIndex, *chip);  // el ancla no se mueve: Mayús+clic sucesivos redefinen el rango
    } else if (mods & Qt::ControlModifier) {
        toggleContiguous(*chip);
        anchor_ = document_.feature(*chip).id();
    } else if (!isSelected(*chip)) {
        selectRange(*chip, *chip);
        anchor_ = document_.feature(*chip).id();
    } else {
        collapseOnRelease_ = selected_.size() > 1;  // quizá empieza un arrastre del bloque
    }
    current_ = document_.feature(*chip).id();
    draggedChip_ = chip;
    update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingMarker_) {
        dropGap_ = gapAt(event->pos().x());
        update();
        return;
    }
    if (draggedChip_ && (event->buttons() & Qt::LeftButton) && (event->pos() - pressPos_).manhattanLength() > 8) {
        dropGap_ = gapAt(event->pos().x());
        collapseOnRelease_ = false;
        setCursor(Qt::ClosedHandCursor);
        autoScroll_->start();
        update();
        return;
    }
    const bool overMarker = std::abs(event->pos().x() - markerX(document_.marker())) <= 6;
    setCursor(overMarker ? Qt::SizeHorCursor : Qt::ArrowCursor);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* /*event*/)
{
    autoScroll_->stop();
    if (draggingMarker_ && dropGap_ && *dropGap_ != document_.marker()) {
        emit markerMoved(*dropGap_);
    } else if (draggedChip_ && dropGap_) {
        // Se mueve el bloque elegido (o solo la ficha si no estaba elegida: el clic ya la eligió sola).
        emitBlockMove(*dropGap_);
    } else if (collapseOnRelease_ && draggedChip_) {
        // Clic sin arrastre sobre una de varias elegidas: queda solo esa.
        selectRange(*draggedChip_, *draggedChip_);
        anchor_ = current_ = document_.feature(*draggedChip_).id();
    }
    collapseOnRelease_ = false;
    draggingMarker_ = false;
    draggedChip_.reset();
    dropGap_.reset();
    unsetCursor();
    update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (const auto chip = chipAt(event->pos())) {
        selectRange(*chip, *chip);
        anchor_ = current_ = document_.feature(*chip).id();
        update();
        emit featureActivated(document_.feature(*chip).id());
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    if (const auto chip = chipAt(event->pos())) {
        if (selected_.size() > 1 && isSelected(*chip)) {
            emit selectionMenuRequested(selection(), event->globalPos());
        } else {
            emit featureMenuRequested(document_.feature(*chip).id(), event->globalPos());
        }
    }
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    // La rueda desplaza la línea de tiempo de lado (no tiene nada que desplazar en vertical).
    if (QScrollBar* bar = scrollBar()) {
        const QPoint delta = event->angleDelta();
        bar->setValue(bar->value() - (delta.y() != 0 ? delta.y() : delta.x()));
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    const std::size_t n = document_.featureCount();
    const Qt::KeyboardModifiers mods = event->modifiers();
    const auto anchorIndex = [&]() -> std::optional<std::size_t> {
        return anchor_ ? document_.indexOf(*anchor_) : std::nullopt;
    };
    // Elige hasta «target» (extendiendo desde el ancla con Mayús) y lo deja a la vista.
    const auto goTo = [&](std::size_t target) {
        if ((mods & Qt::ShiftModifier) && anchorIndex()) {
            selectRange(*anchorIndex(), target);
        } else {
            selectRange(target, target);
            anchor_ = document_.feature(target).id();
        }
        setCurrent(target);
        update();
    };
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (!selected_.empty()) {
            emit deleteRequested(selection());
        }
        return;
    case Qt::Key_Escape:
        clearSelection();
        return;
    case Qt::Key_A:
        if ((mods & Qt::ControlModifier) && n > 0) {
            selectRange(0, n - 1);
            update();
            return;
        }
        break;
    case Qt::Key_Left:
    case Qt::Key_Right: {
        const bool right = event->key() == Qt::Key_Right;
        if (mods & Qt::AltModifier) {
            // Mueve el bloque elegido un lugar.
            if (const auto range = selectionRange()) {
                if (right && range->second + 1 < n) {
                    emitBlockMove(range->second + 2);
                } else if (!right && range->first > 0) {
                    emitBlockMove(range->first - 1);
                }
            }
            return;
        }
        if (n == 0) {
            return;
        }
        const auto here = current_ ? document_.indexOf(*current_) : std::nullopt;
        std::size_t next = right ? 0 : n - 1;
        if (here) {
            next = right ? std::min(n - 1, *here + 1) : (*here == 0 ? 0 : *here - 1);
        }
        goTo(next);
        return;
    }
    case Qt::Key_Home:
        if (n > 0) {
            goTo(0);
        }
        return;
    case Qt::Key_End:
        if (n > 0) {
            goTo(n - 1);
        }
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

bool TimelineWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && !selected_.empty()) {
        // Con la timeline enfocada, Supr, las flechas y Ctrl+A son de la timeline (no de los atajos de la ventana).
        const auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Escape:
            event->accept();
            return true;
        case Qt::Key_A:
            if (key->modifiers() & Qt::ControlModifier) {
                event->accept();
                return true;
            }
            break;
        default:
            break;
        }
    }
    if (event->type() == QEvent::ToolTip) {
        auto* help = static_cast<QHelpEvent*>(event);
        if (const auto chip = chipAt(help->pos())) {
            const document::Feature& f = document_.feature(*chip);
            const QString status = statusText(document_.status(f.id()));
            QToolTip::showText(help->globalPos(),
                               QString::fromStdString(f.name()) + (status.isEmpty() ? QString() : QStringLiteral("\n") + status)
                                   + QStringLiteral("\nMayús+clic: varias seguidas · Supr: borrar · Alt+←/→: mover"));
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
