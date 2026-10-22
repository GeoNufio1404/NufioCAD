#pragma once

// Paneles del modelo: el timeline (abajo) y el navegador (izquierda).

#include <nuf/document/Document.hpp>

#include <QTreeWidget>
#include <QWidget>

#include <optional>
#include <set>
#include <string>

namespace nuf::app {

// Una ficha por operación, en orden, y el marcador (barra naranja) tras la
// última activa. Doble clic edita; clic derecho abre el menú de la ficha;
// arrastrar el marcador vuelve atrás en el historial; arrastrar una ficha la
// mueve de sitio.
class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(const document::Document& document, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Resalta la operación que se está editando (o ninguna).
    void setEditing(std::optional<document::FeatureId> id);

signals:
    void featureActivated(nuf::document::FeatureId id);
    void featureMenuRequested(nuf::document::FeatureId id, const QPoint& globalPos);
    void markerMoved(std::size_t marker);
    // Soltar la ficha «id» en la posición «index» (contada sin ella).
    void featureMoved(nuf::document::FeatureId id, std::size_t index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    bool event(QEvent* event) override;

private:
    QRect chipRect(std::size_t index) const;
    int markerX(std::size_t marker) const;
    std::optional<std::size_t> chipAt(const QPoint& pos) const;
    // x de la separación antes de la ficha «gap» (n = al final).
    int boundaryX(std::size_t gap) const;
    // Separación (0..n) más cercana a la x.
    std::size_t gapAt(int x) const;

    const document::Document& document_;
    std::optional<document::FeatureId> editing_;
    bool draggingMarker_ = false;
    std::optional<std::size_t> draggedChip_;
    QPoint pressPos_;
    std::optional<std::size_t> dropGap_;  // marcador o ficha arrastrados
};

// Árbol con los cuerpos y los sketches; la casilla muestra u oculta.
class NavigatorWidget final : public QTreeWidget {
    Q_OBJECT

public:
    explicit NavigatorWidget(QWidget* parent = nullptr);

    // Vuelve a llenar el árbol con lo que hay en «state» (y los sketches del
    // documento antes de «upTo»).
    void refresh(const document::Document& document, const document::ModelState& state, std::size_t upTo,
                 const std::set<std::string>& hiddenBodies, const std::set<document::FeatureId>& visibleSketches,
                 bool originVisible);

signals:
    void bodyVisibilityChanged(const std::string& key, bool visible);
    void sketchVisibilityChanged(nuf::document::FeatureId id, bool visible);
    void originVisibilityChanged(bool visible);
    void sketchActivated(nuf::document::FeatureId id);

private:
    bool refreshing_ = false;
};

} // namespace nuf::app
