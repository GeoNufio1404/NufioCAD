#pragma once

// Paneles del modelo: el timeline (abajo) y el navegador (izquierda).

#include <nuf/document/Document.hpp>

#include <QScrollBar>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>

#include <utility>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace nuf::app {

// Una ficha por operación, en orden, y el marcador (barra naranja) tras la
// última activa. Doble clic edita; clic derecho abre el menú de la ficha;
// arrastrar el marcador vuelve atrás en el historial; arrastrar una ficha la
// mueve de sitio. Se pueden elegir varias fichas SEGUIDAS (Mayús+clic para un rango, Ctrl+clic para sumar o quitar una
// del extremo, Ctrl+A para todas) y borrarlas (Supr) o moverlas juntas (arrastrando, o Alt+←/→). Si hay más fichas de las
// que caben, se desplaza con la rueda, la barra o las flechas.
class TimelineWidget final : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(const document::Document& document, QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Resalta la operación que se está editando (o ninguna).
    void setEditing(std::optional<document::FeatureId> id);

    // Las operaciones elegidas, en el orden del historial.
    std::vector<document::FeatureId> selection() const;
    void select(const std::vector<document::FeatureId>& ids);
    void clearSelection();
    // Tras un cambio del historial: quita de la elección lo que ya no existe y, si hay fichas nuevas, deja a la vista el
    // marcador.
    void historyChanged();

signals:
    void featureActivated(nuf::document::FeatureId id);
    void featureMenuRequested(nuf::document::FeatureId id, const QPoint& globalPos);
    // Clic derecho sobre una de varias fichas elegidas.
    void selectionMenuRequested(const std::vector<nuf::document::FeatureId>& ids, const QPoint& globalPos);
    void markerMoved(std::size_t marker);
    // Soltar el bloque de fichas «ids» (seguidas, en orden) en la separación «gap» del historial (0..n, contada con ellas).
    void blockMoved(const std::vector<nuf::document::FeatureId>& ids, std::size_t gap);
    void deleteRequested(const std::vector<nuf::document::FeatureId>& ids);

protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
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

    bool isSelected(std::size_t index) const;
    std::vector<std::size_t> selectedIndices() const;
    // El rango (primera, última) si lo elegido es seguido.
    std::optional<std::pair<std::size_t, std::size_t>> selectionRange() const;
    void selectRange(std::size_t from, std::size_t to);
    void toggleContiguous(std::size_t index);
    void setCurrent(std::size_t index);  // la ficha activa para el teclado; la deja a la vista
    void revealRange(int left, int right);
    void emitBlockMove(std::size_t gap);
    void autoScrollStep();
    QScrollBar* scrollBar() const;
    int viewportWidth() const;

    const document::Document& document_;
    std::optional<document::FeatureId> editing_;
    bool draggingMarker_ = false;
    std::optional<std::size_t> draggedChip_;
    QPoint pressPos_;
    std::optional<std::size_t> dropGap_;  // marcador o ficha arrastrados
    std::set<document::FeatureId> selected_;
    std::optional<document::FeatureId> anchor_;   // desde dónde se extiende con Mayús
    std::optional<document::FeatureId> current_;  // la última tocada
    bool collapseOnRelease_ = false;              // se pulsó dentro de varias elegidas: si no se arrastra, queda solo esa
    std::size_t lastCount_ = 0;
    QTimer* autoScroll_ = nullptr;
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
                 bool originVisible, bool groundVisible, const std::set<document::FeatureId>& hiddenPlanes);

    // Las claves de los cuerpos elegidos en el árbol (un componente elegido cuenta con todos sus cuerpos).
    std::vector<std::string> selectedBodies() const;

signals:
    // Lo que se pide con el clic derecho (Fase 21): duplicar, fijar o soltar, agrupar en un componente nuevo, sacar del
    // componente y mover.
    void duplicateRequested(const std::vector<std::string>& keys);
    void fixRequested(const std::vector<std::string>& keys, bool fix);
    void componentRequested(const std::vector<std::string>& keys);
    void ungroupRequested(const std::vector<std::string>& keys);
    void moveRequested(const std::vector<std::string>& keys);
    void bodyVisibilityChanged(const std::string& key, bool visible);
    void sketchVisibilityChanged(nuf::document::FeatureId id, bool visible);
    void originVisibilityChanged(bool visible);
    void groundVisibilityChanged(bool visible);
    void planeVisibilityChanged(nuf::document::FeatureId id, bool visible);
    void componentVisibilityChanged(const std::string& name, bool visible);
    void sketchActivated(nuf::document::FeatureId id);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    bool refreshing_ = false;
    std::set<std::string> fixed_;  // los cuerpos fijos (para el texto del menú)
    std::map<std::string, bool> inComponent_;
};

} // namespace nuf::app
