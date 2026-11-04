#pragma once

#include <QWidget>

#include <vector>

class QAction;
class QFrame;
class QHBoxLayout;
class QMenu;
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

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Page {
        QWidget* widget = nullptr;
        QHBoxLayout* layout = nullptr;
        QHBoxLayout* row = nullptr;  // fila de botones del grupo abierto
        QToolButton* trailing = nullptr;  // botón destacado, en la fila de pestañas
    };

    QToolButton* makeButton(QWidget* parent) const;
    void place(int tab, QWidget* button);

    QTabBar* tabs_ = nullptr;
    QWidget* host_ = nullptr;
    QHBoxLayout* trailingRow_ = nullptr;
    std::vector<Page> pages_;
};

} // namespace nuf::app
