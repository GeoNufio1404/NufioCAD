#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QListWidget;

namespace nuf::app {

// Lista de diseños recientes (la más reciente primero), en QSettings.
class RecentFiles {
public:
    static constexpr int kMax = 10;

    static QStringList list();
    static void add(const QString& path);
    static void remove(const QString& path);
    static void clear();
};

// Pantalla de inicio: flota sobre el visor vacío (como la barra de navegación,
// para no recrear el contexto OpenGL) con los botones de siempre y los
// diseños recientes con su miniatura. La ventana la oculta con la primera acción.
class StartScreen final : public QWidget {
    Q_OBJECT

public:
    // `viewport` es el padre sobre el que flota.
    explicit StartScreen(QWidget* viewport);

    // Vuelve a leer la lista de recientes (y sus miniaturas).
    void reload();

signals:
    void newRequested();
    void openRequested();
    void importRequested();
    void recentRequested(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void reposition();
    void removeSelected();

    QListWidget* list_ = nullptr;
    QLabel* empty_ = nullptr;
};

} // namespace nuf::app
