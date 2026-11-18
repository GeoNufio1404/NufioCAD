#pragma once

#include <QWidget>

#include <vector>

class QAction;
class QFrame;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QMenu;
class QResizeEvent;
class QShowEvent;
class QTabBar;
class QToolButton;

namespace nuf::app {

// Cinta de herramientas al estilo de Fusion 360: pestañas (Sólido, Sketch…)
// y, dentro de cada una, grupos con su nombre debajo (Crear, Modificar…).
// Cada herramienta es un botón grande con icono y nombre; las que tienen
// variantes llevan una flecha con el menú.
class Ribbon final : public QWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget* parent = nullptr);

    int addTab(const QString& title);
    // Abre un grupo nuevo al final de la pestaña; lo siguiente que se agregue
    // a esa pestaña va dentro de él.
    void addGroup(int tab, const QString& title);
    QToolButton* addAction(int tab, QAction* action);
    // Una familia de variantes: el botón se queda con la última usada.
    // Con menu == nullptr es un botón simple.
    QToolButton* addFamily(int tab, QMenu* menu, const std::vector<QAction*>& actions);
    // Botón que solo despliega un menú.
    QToolButton* addMenu(int tab, const QString& text, const QIcon& icon, QMenu* menu);
    // Botón destacado (verde) a la derecha de la fila de pestañas, visible solo
    // con esa pestaña; como «Finalizar sketch» de Fusion 360.
    QToolButton* addTrailing(int tab, QAction* action);

    void setTabVisible(int tab, bool visible);
    void setCurrentTab(int tab);

    // Cómo se adapta la cinta al ancho de la ventana (Fase 20, R3): primero se quitan los nombres bajo los iconos y,
    // si aún no cabe, los últimos grupos se recogen en un botón «Más» con un menú.
    enum class Fit { Full, IconsOnly, Collapsed };
    Fit fit() const { return fit_; }
    // Cuántos grupos de la pestaña actual están recogidos en el menú «Más».
    int hiddenGroups() const { return hiddenGroups_; }
    // Ventana baja: se quitan los nombres de los grupos y la cinta ocupa menos alto (Fase 20, R4).
    void setCompact(bool compact);
    bool compact() const { return compact_; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // Un botón de la cinta y lo que hace falta para repetirlo en el menú «Más».
    struct Entry {
        QToolButton* button = nullptr;
        int textMinWidth = 0;
    };
    struct Group {
        QWidget* widget = nullptr;
        QWidget* separator = nullptr;  // la línea que va antes del grupo (nula en el primero)
        QString title;
        QLabel* label = nullptr;  // el nombre del grupo, bajo los botones
        std::vector<Entry> entries;
    };
    struct Page {
        QWidget* widget = nullptr;
        QHBoxLayout* layout = nullptr;
        QHBoxLayout* row = nullptr;  // fila de botones del grupo abierto
        QToolButton* trailing = nullptr;  // botón destacado, en la fila de pestañas
        std::vector<Group> groups;
    };

    QToolButton* makeButton(QWidget* parent) const;
    void place(int tab, QWidget* button);
    // Recalcula qué cabe en la pestaña actual: nombres, iconos y grupos recogidos.
    void adapt();
    void fillOverflowMenu(const Page& page, int firstHidden);

    QTabBar* tabs_ = nullptr;
    QWidget* host_ = nullptr;
    QHBoxLayout* trailingRow_ = nullptr;
    QToolButton* overflow_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    bool compact_ = false;
    QMenu* overflowMenu_ = nullptr;
    std::vector<Page> pages_;
    Fit fit_ = Fit::Full;
    int hiddenGroups_ = 0;
    bool adapting_ = false;
};

} // namespace nuf::app
