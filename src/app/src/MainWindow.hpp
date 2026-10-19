#pragma once

#include <nuf/commands/CommandStack.hpp>
#include <nuf/document/Document.hpp>

#include <QByteArray>
#include <QMainWindow>

class QAction;
class QDockWidget;

namespace nuf::render {
class Viewport;
}

namespace nuf::app {

// Ventana principal: reproduce la distribución del mockup (navegador a la
// izquierda, propiedades a la derecha, timeline abajo). Cada panel se
// implementa en su fase.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createMenus();
    void createDocks();
    void updateUndoActions();
    void updateTitle();

    document::Document document_;
    commands::CommandStack commands_{document_};

    render::Viewport* viewport_ = nullptr;
    QDockWidget* navigatorDock_ = nullptr;
    QDockWidget* propertiesDock_ = nullptr;
    QDockWidget* timelineDock_ = nullptr;
    QByteArray defaultLayout_; // distribución original, para "Restablecer paneles"
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
};

} // namespace nuf::app
