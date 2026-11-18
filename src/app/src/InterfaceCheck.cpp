#include "InterfaceCheck.hpp"
#include "StallWatch.hpp"
#include "Theme.hpp"

#include "BodiesDialog.hpp"
#include "BusySpinner.hpp"
#include "ExportDialog.hpp"
#include "FeatureTools.hpp"
#include "ParametersDialog.hpp"
#include "SettingsDialog.hpp"
#include "FeatureToolsCommon.hpp"
#include "MainWindow.hpp"
#include "SketchSession.hpp"
#include "Ribbon.hpp"
#include "RadialMenu.hpp"
#include "ValueInput.hpp"
#include "StartScreen.hpp"
#include "Units.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/geometry/DisplayMesh.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/io/DocumentFile.hpp>
#include <nuf/render/Viewport.hpp>
#include <nuf/sketch/Annotations.hpp>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QWheelEvent>
#include <QLabel>
#include <QDockWidget>
#include <QRegularExpression>
#include <QColor>
#include <optional>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QElapsedTimer>
#include <QMenu>
#include <QMenuBar>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMap>
#include <QPushButton>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStatusBar>
#include <QTimer>
#include <QSettings>
#include <QTextStream>
#include <QEvent>
#include <QThread>
#include <QScreen>
#include <QScrollArea>
#include <QAbstractButton>
#include <QToolButton>
#include <QDialog>
#include <QSet>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <numbers>

namespace nuf::app {

InterfaceCheck::InterfaceCheck(MainWindow& window, QString reportPath, int thresholdMs)
    : window_(window)
    , reportPath_(std::move(reportPath))
    , thresholdMs_(thresholdMs)
{
    busyConnection_ = QObject::connect(window_.viewport_, &render::Viewport::busyChanged, window_.viewport_,
                                       [this](bool busy) { viewBusy_ = busy; });
    startWatchdog();
}

InterfaceCheck::~InterfaceCheck()
{
    stopWatchdog();
    QObject::disconnect(busyConnection_);
}

qint64 InterfaceCheck::nowMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void InterfaceCheck::startWatchdog()
{
    mainThread_ = currentThreadHandle();
    heartbeat_ = nowMs();
    watching_ = true;
    // NUF_PRUEBA_PILA_MS: desde cuántos ms de parada se anota la pila (por defecto 1000).
    const qint64 stackAfterMs = std::max(100, qEnvironmentVariableIntValue("NUF_PRUEBA_PILA_MS") > 0 ? qEnvironmentVariableIntValue("NUF_PRUEBA_PILA_MS") : 1000);
    watchdog_ = std::thread([this, stackAfterMs] {
        bool sampled = false;
        while (watching_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (nowMs() - heartbeat_ < stackAfterMs) {
                sampled = false;
                continue;
            }
            if (sampled) {
                continue;
            }
            sampled = true;
            // La interfaz lleva más de un segundo sin dar señales: se copia su pila (ver StallWatch.cpp).
            const QString text = sampleThreadStack(mainThread_);
            std::lock_guard<std::mutex> lock(samplesMutex_);
            samples_.emplace_back(currentStep_.load(), text);
        }
    });
}

void InterfaceCheck::stopWatchdog()
{
    watching_ = false;
    if (watchdog_.joinable()) {
        watchdog_.join();
    }
    releaseThreadHandle(mainThread_);
    mainThread_ = nullptr;
}

void InterfaceCheck::expect(bool condition, const QString& what)
{
    if (current_ != nullptr && !condition) {
        current_->ok = false;
        current_->detail += (current_->detail.isEmpty() ? QString() : QStringLiteral("; ")) + what;
    }
}

void InterfaceCheck::blocking(const QString& what, const std::function<void()>& call)
{
    QElapsedTimer timer;
    timer.start();
    lastAction_ = what;
    heartbeat_ = nowMs();
    call();
    heartbeat_ = nowMs();
    if (current_ != nullptr && timer.elapsed() > current_->blockedMs) {
        current_->blockedMs = timer.elapsed();
        current_->blockedBy = what;
    }
}

void InterfaceCheck::settle(int ms)
{
    QElapsedTimer total;
    total.start();
    while (total.elapsed() < ms) {
        QElapsedTimer one;
        one.start();
        heartbeat_ = nowMs();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        heartbeat_ = nowMs();
        if (current_ != nullptr && one.elapsed() > current_->blockedMs) {
            current_->blockedMs = one.elapsed();  // un evento que paró la interfaz
            current_->blockedBy = QStringLiteral("un evento tras «%1»").arg(lastAction_);
        }
        QThread::msleep(5);
    }
}

bool InterfaceCheck::waitFor(const std::function<bool()>& condition, int timeoutMs)
{
    QElapsedTimer total;
    total.start();
    while (!condition()) {
        if (total.elapsed() > timeoutMs) {
            return false;
        }
        settle(20);
    }
    return true;
}

void InterfaceCheck::waitIdle()
{
    settle(400);  // la vista previa espera 250 ms tras el último cambio
    // Primero lo que corre aparte: al terminar, importar agrega una operación y eso arranca otro cálculo.
    expect(waitFor([this] {
               return !window_.exportRun_.running() && !window_.importRun_.running() && !window_.measureRun_.running();
           }, 120000),
           QStringLiteral("una exportación, importación o medición no terminó en segundo plano"));
    expect(waitFor([this] { return !window_.document_.computing(); }, 120000),
           QStringLiteral("el modelo no terminó de calcularse en segundo plano"));
    expect(waitFor([this] { return window_.tool_ == nullptr || !window_.tool_->previewing(); }, 120000),
           QStringLiteral("la vista previa no terminó de calcularse en segundo plano"));
    expect(waitFor([this] { return !viewBusy_ && !window_.viewport_->preparingDrawing(); }, 60000),
           QStringLiteral("el visor no terminó de preparar el dibujo"));
    settle(100);
}

QDoubleSpinBox* InterfaceCheck::spin(int index) const
{
    if (window_.propertiesDock_ == nullptr || window_.propertiesDock_->widget() == nullptr) {
        return nullptr;
    }
    const auto boxes = window_.propertiesDock_->widget()->findChildren<QDoubleSpinBox*>();
    return index >= 0 && index < boxes.size() ? boxes[index] : nullptr;
}

QPushButton* InterfaceCheck::button(const QString& text) const
{
    if (window_.propertiesDock_ == nullptr || window_.propertiesDock_->widget() == nullptr) {
        return nullptr;
    }
    for (QPushButton* b : window_.propertiesDock_->widget()->findChildren<QPushButton*>()) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

int InterfaceCheck::openChangeAccept(QAction* action, int spinIndex, const QString& choice)
{
    const std::size_t before = window_.document_.featureCount();
    expect(action != nullptr && action->isEnabled(), QStringLiteral("el comando no está disponible"));
    if (action == nullptr || !action->isEnabled()) {
        return 0;
    }
    blocking(QStringLiteral("abrir la herramienta"), [&] { action->trigger(); });
    expect(window_.tool_ != nullptr, QStringLiteral("no se abrió la herramienta"));
    if (window_.tool_ == nullptr) {
        return 0;
    }
    waitIdle();
    if (!choice.isEmpty() && window_.propertiesDock_->widget() != nullptr) {
        for (QComboBox* combo : window_.propertiesDock_->widget()->findChildren<QComboBox*>()) {
            if (const int at = combo->findText(choice); at >= 0) {
                blocking(QStringLiteral("elegir una opción"), [&] { combo->setCurrentIndex(at); });
                waitIdle();
                break;
            }
        }
    }
    if (QDoubleSpinBox* box = spin(spinIndex)) {
        blocking(QStringLiteral("cambiar un valor"), [&] { box->setValue(std::min(box->maximum(), box->value() * 1.1 + 0.05)); });
        waitIdle();
    } else {
        expect(false, QStringLiteral("no hay un cuadro numérico para cambiar"));
    }
    QPushButton* ok = button(QStringLiteral("Aceptar"));
    expect(ok != nullptr && ok->isEnabled(), QStringLiteral("«Aceptar» no está disponible"));
    if (ok != nullptr && ok->isEnabled()) {
        blocking(QStringLiteral("aceptar"), [&] { ok->click(); });
    }
    expect(waitFor([this] { return window_.tool_ == nullptr; }, 10000), QStringLiteral("la herramienta no se cerró al aceptar"));
    waitIdle();
    // Los generadores recuerdan lo aceptado (aquí, valores cambiados a propósito): la prueba siguiente parte de los de fábrica.
    QSettings().remove(QStringLiteral("generator"));
    return int(window_.document_.featureCount() - before);
}

void InterfaceCheck::undoAll()
{
    while (window_.commands_.canUndo()) {
        blocking(QStringLiteral("deshacer"), [&] { window_.commands_.undo(); });
    }
    waitIdle();
    expect(window_.document_.featureCount() == 0, QStringLiteral("quedan operaciones tras deshacer"));
    expect(window_.document_.result().bodies.empty(), QStringLiteral("quedan cuerpos tras deshacer"));
    expect(window_.document_.parameters().empty(), QStringLiteral("quedan parámetros tras deshacer"));
}

void InterfaceCheck::step(const QString& name, const std::function<void()>& body)
{
    // NUF_PRUEBA_SOLO=<texto>: solo los pasos cuyo nombre lo contiene (para repetir uno sin esperar a los 44).
    static const QString only = qEnvironmentVariable("NUF_PRUEBA_SOLO");
    if (!only.isEmpty() && !name.contains(only, Qt::CaseInsensitive)) {
        return;
    }
    results_.push_back({name, true, {}, 0, {}});
    current_ = &results_.back();
    currentStep_ = int(results_.size()) - 1;
    try {
        body();
    } catch (const std::exception& e) {
        expect(false, QStringLiteral("excepción: ") + QString::fromUtf8(e.what()));
    }
    if (current_->blockedMs > thresholdMs_) {
        current_->ok = false;
        current_->detail += (current_->detail.isEmpty() ? QString() : QStringLiteral("; ")) +
                            QStringLiteral("la interfaz se paró %1 ms (límite %2)").arg(current_->blockedMs).arg(thresholdMs_);
    }
    current_ = nullptr;
    currentStep_ = -1;
}

int InterfaceCheck::captureThemes(const QString& folder)
{
    QDir().mkpath(folder);
    settle(300);
    openChangeAccept(window_.boxAction_, 2);
    waitIdle();
    blocking(QStringLiteral("abrir Caja"), [&] { window_.boxAction_->trigger(); });
    waitIdle();
    const std::vector<std::pair<QString, Theme>> themes = {{QStringLiteral("claro"), Theme::Light},
                                                           {QStringLiteral("oscuro"), Theme::Dark}};
    for (const auto& [name, theme] : themes) {
        setTheme(theme);
        settle(700);
        window_.grab().save(QDir(folder).filePath(QStringLiteral("tema-%1-ventana.png").arg(name)));
        int index = 0;
        for (QAction* action : window_.menuBar()->actions()) {
            QMenu* menu = action->menu();
            if (menu == nullptr) {
                continue;
            }
            menu->popup(window_.mapToGlobal(QPoint(40, 40)));
            settle(150);
            menu->grab().save(QDir(folder).filePath(QStringLiteral("tema-%1-menu%2.png").arg(name).arg(index++)));
            menu->close();
        }
    }
    // Los cuadros de diálogo, uno a uno (sin exec: se muestran, se capturan y se cierran).
    const auto shot = [&](QDialog& dialog, const QString& name) {
        dialog.show();
        settle(300);
        dialog.grab().save(QDir(folder).filePath(name));
        dialog.close();
    };
    for (const auto& [name, theme] : themes) {
        setTheme(theme);
        settle(400);
        std::vector<std::string> keys;
        for (const document::Body& body : window_.document_.result().bodies) {
            keys.push_back(body.key);
        }
        ExportDialog exportDialog(window_.document_.result().bodies, keys, &window_);
        shot(exportDialog, QStringLiteral("tema-%1-dialogo-exportar.png").arg(name));
        ParametersDialog parameters({{"alto", "12"}, {"ancho", "alto * 2"}, {"mal", "ancho +"}}, &window_);
        shot(parameters, QStringLiteral("tema-%1-dialogo-parametros.png").arg(name));
        SettingsDialog settings(&window_);
        shot(settings, QStringLiteral("tema-%1-dialogo-configuracion.png").arg(name));
        BodiesDialog bodies(window_.document_, &window_);
        shot(bodies, QStringLiteral("tema-%1-dialogo-cuerpos.png").arg(name));
    }

    // El sketch con restricciones y cotas, en cada tema.
    if (window_.tool_ != nullptr) {
        window_.finishTool(false);
    }
    blocking(QStringLiteral("abrir un sketch"), [&] { window_.startSketch(sketch::Plane::xy(), QStringLiteral("captura"), {}); });
    if (window_.sketch_ != nullptr) {
        SketchSession* session = window_.sketch_.get();
        const auto click = [&](double x, double y) { emit window_.viewport_->sketchClicked(x, y, Qt::NoModifier); };
        session->setTool(SketchSession::Tool::Rectangle);
        click(-30.0, -15.0);
        click(10.0, 12.0);
        session->setTool(SketchSession::Tool::Circle);
        click(30.0, 0.0);
        click(38.0, 0.0);
        session->setTool(SketchSession::Tool::Line);
        click(-30.0, -25.0);
        click(25.0, -25.0);
        session->setTool(SketchSession::Tool::None);
        window_.viewport_->setSketchShowAll(true, true);
        for (const auto& [name, theme] : themes) {
            setTheme(theme);
            settle(500);
            window_.viewport_->grabFramebuffer().save(QDir(folder).filePath(QStringLiteral("tema-%1-sketch.png").arg(name)));
            window_.grab().save(QDir(folder).filePath(QStringLiteral("tema-%1-sketch-ventana.png").arg(name)));
        }
        window_.finishSketch();
        waitIdle();
    }
    // Sin cambios pendientes, para que cerrar la ventana no pregunte si guardar.
    undoAll();
    return 0;
}

namespace {

// Lo que se ve mal en un widget y sus hijos: recortado por su contenedor, fuera de la pantalla o con el texto cortado.
QStringList auditWidgets(QWidget* root, const QRect& limit)
{
    QStringList findings;
    const auto describe = [](const QWidget* w) {
        QString name = QString::fromLatin1(w->metaObject()->className());
        if (!w->objectName().isEmpty()) {
            name += QStringLiteral("«%1»").arg(w->objectName());
        }
        if (const auto* button = qobject_cast<const QAbstractButton*>(w); button != nullptr && !button->text().isEmpty()) {
            name += QStringLiteral(" «%1»").arg(button->text());
        } else if (const auto* label = qobject_cast<const QLabel*>(w); label != nullptr && !label->text().isEmpty()) {
            name += QStringLiteral(" «%1»").arg(label->text().left(30));
        }
        return name;
    };
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (!w->isVisible() || w->width() < 4 || w->height() < 4) {
            continue;
        }
        // Recortado: parte del widget cae fuera del rectángulo de su padre (salvo dentro de una zona con desplazamiento).
        const QWidget* parent = w->parentWidget();
        if (parent != nullptr && qobject_cast<const QScrollArea*>(parent->parentWidget()) == nullptr) {
            const QRect inside = w->geometry().intersected(parent->rect());
            if (inside.width() < w->width() - 2 || inside.height() < w->height() - 2) {
                findings << QStringLiteral("recortado por su contenedor: %1 (%2×%3, se ve %4×%5)")
                                .arg(describe(w)).arg(w->width()).arg(w->height()).arg(inside.width()).arg(inside.height());
                continue;
            }
        }
        // Lo que va dentro de un área con desplazamiento queda fuera de la vista a propósito: se llega a ello desplazando.
        bool scrollable = false;
        for (const QWidget* up = w->parentWidget(); up != nullptr && up != root; up = up->parentWidget()) {
            if (qobject_cast<const QScrollArea*>(up) != nullptr) {
                scrollable = true;
                break;
            }
        }
        const QRect onScreen = QRect(w->mapToGlobal(QPoint(0, 0)), w->size());
        if (!scrollable && (!limit.contains(onScreen.topLeft()) || !limit.contains(onScreen.bottomRight()))) {
            findings << QStringLiteral("fuera de la pantalla: %1").arg(describe(w));
            continue;
        }
        if (const auto* button = qobject_cast<const QAbstractButton*>(w); button != nullptr && !button->text().isEmpty()) {
            const QString text = QString(button->text()).remove(QLatin1Char('&'));
            // Un botón de la cinta lleva el texto bajo el icono (todo el ancho es del texto) o solo el icono (no se dibuja).
            const auto* tool = qobject_cast<const QToolButton*>(w);
            const bool textHidden = tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonIconOnly;
            const bool textBelow = tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonTextUnderIcon;
            const int available = button->width() - ((button->icon().isNull() || textBelow) ? 12 : 12 + button->iconSize().width());
            if (!textHidden && button->fontMetrics().horizontalAdvance(text) > available) {
                findings << QStringLiteral("texto cortado: %1 (el texto mide %2 px y caben %3)")
                                .arg(describe(w)).arg(button->fontMetrics().horizontalAdvance(text)).arg(available);
            }
        } else if (const auto* label = qobject_cast<const QLabel*>(w);
                   label != nullptr && !label->wordWrap() && label->pixmap().isNull() && !label->text().isEmpty()
                   && !label->text().startsWith(QLatin1Char('<'))) {
            if (label->fontMetrics().horizontalAdvance(label->text()) > label->width() + 1) {
                findings << QStringLiteral("texto cortado: %1 (el texto mide %2 px y caben %3)")
                                .arg(describe(w)).arg(label->fontMetrics().horizontalAdvance(label->text())).arg(label->width());
            }
        }
    }
    return findings;
}

} // namespace

int InterfaceCheck::captureInventory(const QString& folder)
{
    QDir().mkpath(folder);
    QFile report(QDir(folder).filePath(QStringLiteral("inventario.txt")));
    if (!report.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return 1;
    }
    QTextStream out(&report);
    out.setEncoding(QStringConverter::Utf8);
    const QScreen* screen = window_.screen();
    const QRect available = screen->availableGeometry();
    window_.move(available.topLeft());  // la posición inicial de la ventana no debe contar como «fuera de la pantalla»
    settle(500);
    out << "Pantalla: " << screen->geometry().width() << "x" << screen->geometry().height() << " lógicos, disponibles "
        << available.width() << "x" << available.height() << ", escala " << screen->devicePixelRatio() << ", DPI lógico "
        << screen->logicalDotsPerInch() << "\n";
    out << "Ventana principal: " << window_.width() << "x" << window_.height() << ", mínimo que pide su contenido "
        << window_.minimumSizeHint().width() << "x" << window_.minimumSizeHint().height() << "\n";
    const bool windowFits = window_.minimumSizeHint().width() <= available.width() && window_.minimumSizeHint().height() <= available.height();
    out << (windowFits ? "OK: la ventana puede caber en la pantalla\n"
                       : "PROBLEMA: el contenido de la ventana exige más que la pantalla disponible: parte queda fuera\n");
    int problems = windowFits ? 0 : 1;

    const QRect screenRect = screen->geometry();
    const auto record = [&](const QString& scene, QWidget* widget, const QString& file) {
        settle(300);
        if (widget != nullptr && !file.isEmpty()) {
            widget->grab().save(QDir(folder).filePath(file));
        }
        QStringList findings = widget != nullptr ? auditWidgets(widget, screenRect) : QStringList();
        findings.removeDuplicates();
        out << "\n== " << scene << (file.isEmpty() ? QString() : QStringLiteral("  [") + file + QStringLiteral("]")) << " ==\n";
        if (widget != nullptr) {
            out << "tamaño " << widget->width() << "x" << widget->height() << ", pide como mínimo " << widget->minimumSizeHint().width()
                << "x" << widget->minimumSizeHint().height() << ", preferido " << widget->sizeHint().width() << "x"
                << widget->sizeHint().height() << "\n";
        }
        if (findings.isEmpty()) {
            out << "sin problemas encontrados\n";
        }
        for (const QString& f : findings) {
            out << "- " << f << "\n";
        }
        problems += int(findings.size());
        // Lo que la ventana pide para esta escena frente a la pantalla: si no cabe, la ventana crece y se sale.
        if (widget == &window_) {
            const Ribbon::Fit fit = window_.ribbon_->fit();
            out << "cinta: ancho " << window_.ribbon_->width() << ", "
                << (fit == Ribbon::Fit::Full ? "todo a la vista" : fit == Ribbon::Fit::IconsOnly ? "solo iconos" : "grupos recogidos en «Más»")
                << ", grupos recogidos " << window_.ribbon_->hiddenGroups() << "\n";
            const QSize need = widget->minimumSizeHint();
            if (need.width() > available.width() || need.height() > available.height()) {
                out << "- la ventana con esto abierto pide " << need.width() << "x" << need.height() << " y la pantalla disponible es "
                    << available.width() << "x" << available.height() << ": no cabe\n";
                ++problems;
            }
        }
    };
    const auto panel = [&](const QString& scene, const QString& file) {
        record(scene, &window_, file);
        if (window_.propertiesDock_ != nullptr && window_.propertiesDock_->widget() != nullptr && window_.propertiesDock_->isVisible()) {
            QWidget* content = window_.propertiesDock_->widget();
            const bool scrolls = content->findChild<QScrollArea*>() != nullptr || qobject_cast<QScrollArea*>(content) != nullptr;
            out << "panel de herramienta: " << window_.propertiesDock_->width() << "x" << window_.propertiesDock_->height()
                << ", su contenido necesita " << content->sizeHint().width() << "x" << content->sizeHint().height()
                << (scrolls ? ", con desplazamiento" : ", SIN desplazamiento") << "\n";
            if (!scrolls && content->sizeHint().width() > window_.propertiesDock_->width()) {
                out << "- el contenido del panel es más ancho que el panel (" << content->sizeHint().width() << " frente a "
                    << window_.propertiesDock_->width() << "): se corta por los lados\n";
                ++problems;
            }
            if (!scrolls && content->sizeHint().height() > window_.propertiesDock_->height()) {
                out << "- el panel no cabe en alto y no se puede desplazar\n";
                ++problems;
            }
        }
    };

    // La ventana vacía, con cada pestaña de la cinta.
    for (int tab : {0, 2}) {
        window_.ribbon_->setCurrentTab(tab);
        panel(QStringLiteral("Ventana, pestaña %1 de la cinta").arg(tab), QStringLiteral("ventana-pestana%1.png").arg(tab));
    }
    window_.ribbon_->setCurrentTab(0);

    // Cada herramienta con panel y cada generador: se abre, se captura y se cancela.
    std::vector<std::pair<QString, QAction*>> tools = {{QStringLiteral("Caja"), window_.boxAction_},
                                                       {QStringLiteral("Cilindro"), window_.cylinderAction_},
                                                       {QStringLiteral("Engranaje"), window_.gearAction_},
                                                       {QStringLiteral("Rosca"), window_.threadAction_},
                                                       {QStringLiteral("Barrido"), window_.sweepAction_},
                                                       {QStringLiteral("Loft"), window_.loftAction_},
                                                       {QStringLiteral("Plano de construcción"), window_.planeAction_},
                                                       {QStringLiteral("Eje de construcción"), window_.axisAction_},
                                                       {QStringLiteral("Medir"), window_.measureAction_}};
    for (QAction* generator : window_.generatorActions_) {
        tools.emplace_back(QStringLiteral("Generador «%1»").arg(generator->text()), generator);
    }
    int toolNumber = 0;
    for (const auto& [name, action] : tools) {
        ++toolNumber;
        if (action == nullptr || !action->isEnabled()) {
            out << "\n== " << name << " ==\nno disponible en esta ventana (se omite)\n";
            continue;
        }
        action->trigger();
        waitIdle();
        if (window_.tool_ == nullptr) {
            out << "\n== " << name << " ==\nno abrió un panel (se omite)\n";
            continue;
        }
        panel(name, QStringLiteral("herramienta-%1.png").arg(toolNumber, 2, 10, QLatin1Char('0')));
        window_.finishTool(false);
        waitIdle();
    }

    // Los diálogos.
    const auto dialogScene = [&](QDialog& dialog, const QString& name, const QString& file) {
        dialog.show();
        settle(300);
        record(name, &dialog, file);
        const QRect frame = dialog.frameGeometry();
        if (!available.contains(frame)) {
            out << "- el diálogo se sale de la pantalla disponible (" << frame.width() << "x" << frame.height() << " en " << frame.x()
                << "," << frame.y() << ")\n";
            ++problems;
        }
        dialog.close();
    };
    std::vector<std::string> keys;
    for (const document::Body& body : window_.document_.result().bodies) {
        keys.push_back(body.key);
    }
    ExportDialog exportDialog(window_.document_.result().bodies, keys, &window_);
    dialogScene(exportDialog, QStringLiteral("Diálogo Exportar"), QStringLiteral("dialogo-exportar.png"));
    ParametersDialog parameters({{"alto", "12"}, {"ancho", "alto * 2"}, {"mal", "ancho +"}}, &window_);
    dialogScene(parameters, QStringLiteral("Diálogo Parámetros"), QStringLiteral("dialogo-parametros.png"));
    SettingsDialog settings(&window_);
    dialogScene(settings, QStringLiteral("Diálogo Configuración"), QStringLiteral("dialogo-configuracion.png"));
    BodiesDialog bodies(window_.document_, &window_);
    dialogScene(bodies, QStringLiteral("Diálogo Cuerpos"), QStringLiteral("dialogo-cuerpos.png"));

    // El sketch con su cinta.
    blocking(QStringLiteral("abrir un sketch"), [&] { window_.startSketch(sketch::Plane::xy(), QStringLiteral("inventario"), {}); });
    if (window_.sketch_ != nullptr) {
        window_.ribbon_->setCurrentTab(1);
        panel(QStringLiteral("Sketch (cinta del sketch)"), QStringLiteral("sketch.png"));
        window_.finishSketch();
        waitIdle();
    }
    out << "\nTotal de problemas encontrados: " << problems << "\n";
    out.flush();
    undoAll();
    // Código de salida: 0 si todo cabe y se ve; 3 si hay problemas (así una prueba automática puede fallar).
    return problems > 0 ? 3 : 0;
}

int InterfaceCheck::run()
{
    settle(300);
    step(QStringLiteral("La ventana empieza vacía"), [&] {
        expect(window_.document_.featureCount() == 0, QStringLiteral("el diseño no está vacío"));
        expect(window_.tool_ == nullptr, QStringLiteral("hay una herramienta abierta"));
    });

    // Fase 15: menú radial con clic derecho (U6) y valor principal junto al cursor (U7).
    step(QStringLiteral("Menú radial con clic derecho en la vista"), [&] {
        const QPointF at(300.0, 300.0);
        QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, at, at, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(window_.viewport_, &press);
        QApplication::sendEvent(window_.viewport_, &release);
        settle(100);
        auto* menu = window_.viewport_->findChild<RadialMenu*>();
        expect(menu != nullptr && menu->isVisible(), QStringLiteral("no se abrió el menú radial"));
        if (menu != nullptr) {
            expect(menu->count() == 8, QStringLiteral("el menú radial debería tener 8 sectores"));
            // El sector de la Caja (el cuarto): se elige y abre la herramienta.
            const int boxSector = 3;
            expect(menu->choose(boxSector), QStringLiteral("elegir un sector no lanzó la acción"));
            settle(100);
            expect(window_.tool_ != nullptr, QStringLiteral("el sector no abrió la herramienta"));
            // Escribir un número en la vista abre el valor principal junto al cursor (U7).
            QKeyEvent key(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier, QStringLiteral("3"));
            QApplication::sendEvent(window_.viewport_, &key);
            expect(window_.toolValueInput_ != nullptr && window_.toolValueInput_->isVisible(),
                   QStringLiteral("escribir en la vista no abrió el cuadro del valor"));
            if (window_.toolValueInput_ != nullptr && window_.tool_ != nullptr) {
                const FeatureTool::PrimaryField primary = window_.tool_->primaryField();
                expect(primary.box != nullptr, QStringLiteral("la herramienta no tiene campo principal"));
                window_.toolValueInput_->startTyping(QStringLiteral("37"));
                emit window_.toolValueInput_->accepted();
                if (primary.box != nullptr) {
                    expect(std::abs(primary.box->value() - 37.0) < 1e-6,
                           QStringLiteral("el valor escrito no llegó al campo (%1)").arg(primary.box->value()));
                }
            }
            window_.finishTool(false);
            waitIdle();
        }
        settle(100);  // deleteLater
        {
            const auto* left = window_.viewport_->findChild<RadialMenu*>();
            expect(left == nullptr || !left->isVisible(), QStringLiteral("el menú radial no se cerró"));
        }
    });

    // Cada generador: abrir, cambiar un valor, aceptar (queda todo), deshacer (no queda nada).
    const std::vector<QAction*> generators = window_.generatorActions_;
    for (QAction* action : generators) {
        step(QStringLiteral("Generador «%1»").arg(action->text()), [&] {
            const int added = openChangeAccept(action, 0);
            expect(added >= 1, QStringLiteral("no añadió operaciones"));
            waitIdle();
            expect(!window_.document_.result().bodies.empty(), QStringLiteral("no creó cuerpos"));
            undoAll();
        });
    }

    // Las herramientas con vista previa pesada o con colocación.
    const std::vector<std::pair<QString, QAction*>> tools = {{QStringLiteral("Rosca"), window_.threadAction_},
                                                             {QStringLiteral("Engranaje"), window_.gearAction_},
                                                             {QStringLiteral("Caja"), window_.boxAction_},
                                                             {QStringLiteral("Cilindro"), window_.cylinderAction_}};
    for (const auto& [name, action] : tools) {
        step(QStringLiteral("Herramienta «%1»: abrir, cambiar, aceptar y deshacer").arg(name), [&] {
            const int added = openChangeAccept(action, 2);
            expect(added == 1, QStringLiteral("debía añadir una operación"));
            waitIdle();
            expect(window_.document_.result().bodies.size() == 1, QStringLiteral("debía crear un cuerpo"));
            undoAll();
        });
    }
    step(QStringLiteral("Herramienta «Rosca»: cancelar no cambia nada"), [&] {
        blocking(QStringLiteral("abrir Rosca"), [&] { window_.threadAction_->trigger(); });
        expect(window_.tool_ != nullptr, QStringLiteral("no se abrió la herramienta"));
        waitIdle();
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            blocking(QStringLiteral("cancelar"), [&] { cancel->click(); });
        } else {
            expect(false, QStringLiteral("no hay botón «Cancelar»"));
        }
        expect(waitFor([this] { return window_.tool_ == nullptr; }, 10000), QStringLiteral("no se cerró al cancelar"));
        waitIdle();
        expect(window_.document_.featureCount() == 0, QStringLiteral("cancelar cambió el diseño"));
    });

    // Unidades: cambian los cuadros y el texto; se restauran al terminar.
    step(QStringLiteral("Unidades: centímetros y vuelta a milímetros"), [&] {
        const LengthUnit original = Units::instance().length();
        Units::instance().setLength(LengthUnit::Cm);
        settle(150);
        blocking(QStringLiteral("abrir Caja"), [&] { window_.boxAction_->trigger(); });
        waitIdle();
        if (QDoubleSpinBox* width = spin(3)) {
            expect(width->text().startsWith(QStringLiteral("2")), QStringLiteral("20 mm no se ve como 2 cm: «%1»").arg(width->text()));
        } else {
            expect(false, QStringLiteral("no hay cuadro de ancho"));
        }
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            cancel->click();
        }
        waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        Units::instance().setLength(original);
        settle(150);
    });

    // La flecha de doble punta de la medida que se edita: al enfocar un cuadro deben verse píxeles turquesa.
    // Píxeles del color exacto de la flecha que tiene que sumar una flecha por pequeña que sea. Sin flecha el conteo es
    // siempre 0 (no hay otra cosa de ese color), así que no hace falta un mínimo alto: la más pequeña (el ángulo de hélice
    // del engranaje) suma unos 35.
    constexpr long kMinArrowPixels = 15;
    const auto arrowCheck = [&](const QString& name, QAction* action, int spinIndex, bool needsBody = false,
                                std::optional<double> preset = std::nullopt) {
        step(QStringLiteral("Flecha de la medida que se edita: %1").arg(name), [&] {
            if (needsBody) {
                expect(openChangeAccept(window_.boxAction_, 3) == 1, QStringLiteral("no se creó la caja de apoyo"));
            }
            blocking(QStringLiteral("abrir la herramienta"), [&] { action->trigger(); });
            waitIdle();
            // Píxeles turquesa de la flecha: «amplio» (cualquier turquesa, también el de un plano de construcción o un
            // arco medio tapado) y «exacto» (el color de la flecha, 3FD0E0, sin mezclar con nada: solo lo que dibuja la
            // flecha, aunque el fondo ya sea turquesa).
            const auto turquoise = [&](bool exact = false) {
                const QImage image = window_.viewport_->grabFramebuffer();
                long count = 0;
                for (int y = 0; y < image.height(); ++y) {
                    for (int x = 0; x < image.width(); ++x) {
                        const QColor c = image.pixelColor(x, y);
                        const bool match = exact ? std::abs(c.red() - 0x3F) < 18 && std::abs(c.green() - 0xD0) < 18 &&
                                                       std::abs(c.blue() - 0xE0) < 18
                                                 : c.red() < 140 && c.green() > 170 && c.blue() > 180 && c.green() - c.red() > 70;
                        if (match) {
                            ++count;
                        }
                    }
                }
                return count;
            };
            QDoubleSpinBox* box = spin(spinIndex);
            expect(box != nullptr, QStringLiteral("no hay cuadro numérico"));
            if (box != nullptr && preset) {
                blocking(QStringLiteral("poner un valor"), [&] { box->setValue(*preset); });
                waitIdle();
            }
            // Como el usuario que se acerca a la pieza: una medida pequeña (el radio de una esquina) en una vista
            // lejana serían un par de píxeles.
            blocking(QStringLiteral("encuadrar"), [&] { window_.viewport_->fitAll(); });
            settle(300);
            if (box != nullptr) {
                window_.activateWindow();
                window_.propertiesDock_->widget()->setFocus();
                settle(200);
                const long without = turquoise();
                const long withoutExact = turquoise(true);
                // Windows no siempre deja que una ventana recién abierta tome el primer plano (y entonces el cuadro no
                // tiene foco aunque se lo pidamos): se reintenta unas veces antes de dar el foco por perdido.
                const auto focusBox = [&] {
                    for (int attempt = 0; attempt < 8 && !box->hasFocus(); ++attempt) {
                        window_.raise();
                        window_.activateWindow();
                        box->setFocus(Qt::OtherFocusReason);
                        settle(150);
                    }
                };
                focusBox();
                settle(400);
                const long with = turquoise();
                const long withExact = turquoise(true);
                expect(box->hasFocus(), QStringLiteral("el cuadro no recibió el foco"));
                // Se cuenta el color exacto de la flecha: así una flecha pequeña o sobre un plano que ya es turquesa se
                // distingue igual (antes, contar cualquier turquesa sobre un mínimo fijo fallaba por 1–4 píxeles).
                expect(withExact > withoutExact + kMinArrowPixels,
                       QStringLiteral("no se dibujó la flecha (píxeles del color de la flecha: %1 sin foco, %2 con foco; turquesa en general %3 → %4)")
                           .arg(withoutExact).arg(withExact).arg(without).arg(with));
                if (withExact <= withoutExact + kMinArrowPixels) {
                    // Para ver qué pasó: la vista tal como quedó.
                    QString file = name;
                    file.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")), QStringLiteral("_"));
                    window_.viewport_->grabFramebuffer().save(QDir::temp().filePath(QStringLiteral("flecha-%1.png").arg(file)));
                }
                // Al escribir otro valor la flecha sigue ahí (se recoloca con la vista previa).
                blocking(QStringLiteral("cambiar el valor"), [&] { box->setValue(std::min(box->maximum(), box->value() + 5.0)); });
                waitIdle();  // la vista previa del engranaje tarda ~0,7 s en Debug y la flecha se recoloca al terminar
                if (!box->hasFocus()) {
                    // Si algo le quitó el foco a la ventana mientras se calculaba, la flecha (que sigue al foco) se quita
                    // con razón: se le devuelve y se espera a que reaparezca.
                    focusBox();
                    settle(500);
                }
                const long changed = turquoise();
                const long changedExact = turquoise(true);
                expect(changedExact > withoutExact + kMinArrowPixels,
                       QStringLiteral("la flecha desapareció al cambiar el valor (%1 píxeles del color de la flecha)").arg(changedExact));
                if (current_ != nullptr) {
                    current_->detail += QStringLiteral(" turquesa %1 → %2 → %3; color de la flecha %4 → %5 → %6")
                                            .arg(without).arg(with).arg(changed).arg(withoutExact).arg(withExact).arg(changedExact);
                }
            }
            if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
                cancel->click();
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
            waitIdle();
            if (needsBody) {
                undoAll();
            }
        });
    };
    arrowCheck(QStringLiteral("Caja"), window_.boxAction_, 3);
    arrowCheck(QStringLiteral("Cilindro"), window_.cylinderAction_, 3);
    arrowCheck(QStringLiteral("Rosca"), window_.threadAction_, 2);
    arrowCheck(QStringLiteral("Engranaje"), window_.gearAction_, 2);
    arrowCheck(QStringLiteral("Plano (ángulo)"), window_.planeAction_, 1, false, 30.0);
    arrowCheck(QStringLiteral("Mover cuerpo (X)"), window_.moveAction_, 0, true, 20.0);
    arrowCheck(QStringLiteral("Mover cuerpo (giro en X)"), window_.moveAction_, 3, true, 45.0);
    arrowCheck(QStringLiteral("Patrón (distancia)"), window_.patternAction_, 1, true);
    arrowCheck(QStringLiteral("Caja (radio de esquinas)"), window_.boxAction_, 6, false, 3.0);
    arrowCheck(QStringLiteral("Engranaje (ángulo de presión)"), window_.gearAction_, 5);
    arrowCheck(QStringLiteral("Engranaje (giro inicial)"), window_.gearAction_, 4, false, 30.0);
    arrowCheck(QStringLiteral("Engranaje (ángulo de hélice)"), window_.gearAction_, 8, false, 15.0);
    for (QAction* action : generators) {
        const QString name = action->text();
        if (name.contains(QStringLiteral("Caja con tapa")) || name.contains(QStringLiteral("Lightbox"))) {
            arrowCheck(name, action, 0);
        }
        if (name.contains(QStringLiteral("Par de engranajes"))) {
            arrowCheck(name + QStringLiteral(" (módulo)"), action, 0);
        }
    }

    // La vista previa de un generador es azul translúcida y, al terminar de calcular, no queda el «Calculando…».
    step(QStringLiteral("Vista previa translúcida de un generador"), [&] {
        QAction* lightbox = nullptr;
        for (QAction* action : generators) {
            if (action->text().contains(QStringLiteral("Lightbox"))) {
                lightbox = action;
            }
        }
        expect(lightbox != nullptr && lightbox->isEnabled(), QStringLiteral("no hay generador Letrero"));
        if (lightbox == nullptr) {
            return;
        }
        blocking(QStringLiteral("abrir la herramienta"), [&] { lightbox->trigger(); });
        waitIdle();
        settle(300);
        const QImage image = window_.viewport_->grabFramebuffer();
        long bluish = 0;
        long onEdge = 0;  // azul pegado al borde de la vista: la pieza no cabe (el encuadre se hizo antes de tener los cuerpos)
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                if (c.blue() > c.red() + 55 && c.blue() > c.green() + 25 && c.green() < 170) {
                    ++bluish;
                    if (x < 3 || y < 3 || x >= image.width() - 3 || y >= image.height() - 3) {
                        ++onEdge;
                    }
                }
            }
        }
        expect(onEdge == 0, QStringLiteral("la vista previa no cabe en la vista: %1 píxeles azules en el borde").arg(onEdge));
        if (const QString folder = qEnvironmentVariable("NUF_PRUEBA_CAPTURAS"); !folder.isEmpty()) {
            image.save(QDir(folder).filePath(QStringLiteral("vista-previa-translucida.png")));  // para mirarla a ojo
        }
        expect(bluish > 500, QStringLiteral("la vista previa no se ve azul translúcida (%1 píxeles azulados)").arg(bluish));
        expect(window_.busyBadge_ != nullptr && !window_.busyBadge_->isVisible(), QStringLiteral("«Calculando…» sigue a la vista al terminar"));
        if (current_ != nullptr) {
            current_->detail += QStringLiteral(" azulados %1").arg(bluish);
        }
        if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
            cancel->click();
        }
        waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        waitIdle();
    });

    // Mover y girar con el ratón: se simulan las señales del visor (agarrar, arrastrar, soltar) para comprobar que los
    // cuadros siguen al arrastre sin calcular, que el giro se pega a las marcas de 45° y que al soltar se calcula.
    step(QStringLiteral("Mover y girar arrastrando (con sombra)"), [&] {
        expect(openChangeAccept(window_.boxAction_, 3) == 1, QStringLiteral("no se creó la caja de apoyo"));
        blocking(QStringLiteral("abrir Mover"), [&] { window_.moveAction_->trigger(); });
        expect(window_.tool_ != nullptr, QStringLiteral("no se abrió Mover"));
        waitIdle();
        QDoubleSpinBox* x = spin(0);
        QDoubleSpinBox* rz = spin(5);
        expect(x != nullptr && rz != nullptr, QStringLiteral("faltan los cuadros de mover y girar"));
        if (x != nullptr && rz != nullptr && window_.tool_ != nullptr) {
            // Las flechas (rojo, verde y azul del gizmo) se ven en la vista; la captura queda en %TEMP% para mirarla.
            const auto gizmoPixels = [&](const QString& file) {
                blocking(QStringLiteral("encuadrar"), [&] { window_.viewport_->fitAll(); });
                settle(1500);  // el gizmo se quita mientras cambia el zoom y vuelve cuando la vista lleva 900 ms quieta
                const QImage image = window_.viewport_->grabFramebuffer();
                image.save(QDir::temp().filePath(file));
                long red = 0, green = 0, blue = 0;
                for (int py = 0; py < image.height(); ++py) {
                    for (int px = 0; px < image.width(); ++px) {
                        const QColor c = image.pixelColor(px, py);
                        red += c.red() > 190 && c.green() < 110 && c.blue() < 120;
                        green += c.green() > 160 && c.red() < 110 && c.blue() < 150 && c.green() - c.blue() > 50;
                        blue += c.blue() > 200 && c.red() < 120 && c.green() > 100 && c.green() < 180;
                    }
                }
                return std::array<long, 3>{red, green, blue};
            };
            const auto arrows = gizmoPixels(QStringLiteral("gizmo-mover.png"));
            expect(arrows[0] > 80 && arrows[1] > 80 && arrows[2] > 80,
                   QStringLiteral("no se ven las tres flechas (rojo %1, verde %2, azul %3)").arg(arrows[0]).arg(arrows[1]).arg(arrows[2]));
            if (QPushButton* turn = button(QStringLiteral("Girar"))) {
                turn->click();
            }
            const auto rings = gizmoPixels(QStringLiteral("gizmo-girar.png"));
            expect(rings[0] > 80 && rings[1] > 80 && rings[2] > 80,
                   QStringLiteral("no se ven los tres anillos (rojo %1, verde %2, azul %3)").arg(rings[0]).arg(rings[1]).arg(rings[2]));
            if (current_ != nullptr) {
                current_->detail += QStringLiteral(" flechas %1/%2/%3 anillos %4/%5/%6")
                                        .arg(arrows[0]).arg(arrows[1]).arg(arrows[2]).arg(rings[0]).arg(rings[1]).arg(rings[2]);
            }
            // Lo que cuesta dibujar de nuevo los anillos (es lo que se paga al volver a aparecer tras un zoom).
            {
                long rebuild = 0;
                for (int i = 0; i < 5; ++i) {
                    window_.viewport_->hideGizmo();
                    QElapsedTimer timer;
                    timer.start();
                    blocking(QStringLiteral("dibujar los anillos"), [&] {
                        window_.viewport_->showGizmo(render::Viewport::GizmoKind::Rotate, {0.0, 0.0, 0.0});
                        window_.viewport_->grabFramebuffer();  // fuerza el dibujo del cuadro, que es donde se mallan
                    });
                    rebuild = std::max<long>(rebuild, timer.elapsed());
                }
                expect(rebuild < 400, QStringLiteral("dibujar los anillos tardó %1 ms").arg(rebuild));
                if (current_ != nullptr) {
                    current_->detail += QStringLiteral(" anillos %1 ms").arg(rebuild);
                }
            }
            // Zoom con la rueda con los anillos puestos: antes se rehacían a cada cuadro y la vista iba a 1 imagen por
            // segundo. Ahora se quitan mientras cambia el zoom y vuelven al quedar quieta la vista.
            {
                long slowest = 0;
                const QPointF center(window_.viewport_->width() / 2.0, window_.viewport_->height() / 2.0);
                const double pixelBefore = window_.viewport_->sketchPixelSize();
                for (int i = 0; i < 12; ++i) {
                    if (i == 6) {
                        expect(std::abs(window_.viewport_->sketchPixelSize() - pixelBefore) > 1e-6 * pixelBefore,
                               QStringLiteral("la rueda no cambió el zoom (la prueba no mide nada)"));
                    }
                    QElapsedTimer one;
                    one.start();
                    blocking(QStringLiteral("zoom con la rueda"), [&] {
                        QWheelEvent wheel(center, window_.viewport_->mapToGlobal(center), QPoint(), QPoint(0, i < 6 ? 120 : -120),
                                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                        QCoreApplication::sendEvent(window_.viewport_, &wheel);
                    });
                    settle(40);  // deja que se dibuje el cuadro
                    slowest = std::max<long>(slowest, one.elapsed());
                }
                expect(slowest < 700, QStringLiteral("un paso de zoom con los anillos tardó %1 ms").arg(slowest));
                if (current_ != nullptr) {
                    current_->detail += QStringLiteral(" zoom máx. %1 ms").arg(slowest);
                }
                settle(1300);  // vuelven los anillos
            }
            if (QPushButton* move = button(QStringLiteral("Mover"))) {
                move->click();
            }
            // Mover: arrastrar la flecha X 12,5 mm.
            blocking(QStringLiteral("arrastrar la flecha X"), [&] {
                emit window_.viewport_->gizmoPressed(0);
                emit window_.viewport_->gizmoDragged(0, 12.5);
            });
            settle(400);
            // La sombra (azul translúcida) se ve mientras se arrastra: antes de quitar el redibujado forzado se veía, y al
            // quitarlo la vista no se enteraba de que había cambiado (771 píxeles azulados, solo las flechas, contra 137 000).
            const auto countPixels = [&](const std::function<bool(const QColor&)>& wanted) {
                const QImage image = window_.viewport_->grabFramebuffer();
                long count = 0;
                for (int py = 0; py < image.height(); ++py) {
                    for (int px = 0; px < image.width(); ++px) {
                        count += wanted(image.pixelColor(px, py)) ? 1 : 0;
                    }
                }
                return count;
            };
            const long ghostPixels = countPixels([](const QColor& c) { return c.blue() > c.red() + 40 && c.blue() > c.green() + 15 && c.green() < 190; });
            expect(ghostPixels > 8000, QStringLiteral("no se ve la sombra durante el arrastre (%1 píxeles azulados)").arg(ghostPixels));
            if (current_ != nullptr) {
                current_->detail += QStringLiteral(" sombra: %1 px").arg(ghostPixels);
            }
            expect(std::abs(x->value() - 12.5) < 0.01, QStringLiteral("el cuadro X no siguió al arrastre (%1)").arg(x->value()));
            expect(!window_.tool_->previewing(), QStringLiteral("se calculó la vista previa durante el arrastre"));
            blocking(QStringLiteral("soltar la flecha X"), [&] { emit window_.viewport_->gizmoReleased(0); });
            waitIdle();
            // Girar: el anillo Z, 43,5° (cerca de la marca de 45°) y luego 60° (sin marca cerca).
            if (QPushButton* turn = button(QStringLiteral("Girar"))) {
                turn->click();
            }
            // Mientras se arrastra el anillo Z, los anillos X (rojo) e Y (verde) se quitan para dejar la vista limpia, y al
            // soltar vuelven.
            const auto ringRed = [](const QColor& c) { return c.red() > 190 && c.green() < 110 && c.blue() < 120; };
            const auto ringGreen = [](const QColor& c) { return c.green() > 160 && c.red() < 110 && c.blue() < 150 && c.green() - c.blue() > 50; };
            settle(200);
            const long redBefore = countPixels(ringRed);
            const long greenBefore = countPixels(ringGreen);
            blocking(QStringLiteral("arrastrar el anillo Z"), [&] {
                emit window_.viewport_->gizmoPressed(2);
                emit window_.viewport_->gizmoDragged(2, 43.5 * std::numbers::pi / 180.0);
            });
            settle(250);
            const long redDuring = countPixels(ringRed);
            const long greenDuring = countPixels(ringGreen);
            expect(redBefore - redDuring > 400 && greenBefore - greenDuring > 400,
                   QStringLiteral("los otros anillos no se quitaron al arrastrar uno (rojo %1→%2, verde %3→%4)")
                       .arg(redBefore).arg(redDuring).arg(greenBefore).arg(greenDuring));
            if (current_ != nullptr) {
                current_->detail += QStringLiteral(" otros anillos al arrastrar: rojo %1→%2, verde %3→%4")
                                        .arg(redBefore).arg(redDuring).arg(greenBefore).arg(greenDuring);
            }
            expect(std::abs(rz->value() - 45.0) < 0.01, QStringLiteral("el giro no se pegó a la marca de 45° (%1)").arg(rz->value()));
            // Pegado a la marca: se dibuja la recta amarilla de «alineado» y el giro se queda ahí (softblock) aunque el
            // cursor se aleje un poco (51° está a 6° de la marca: se mantiene; más lejos, a 60°, se suelta).
            const auto yellow = [](const QColor& c) { return c.red() > 230 && c.green() > 190 && c.blue() < 130; };
            settle(200);
            window_.viewport_->grabFramebuffer().save(QDir::temp().filePath(QStringLiteral("gizmo-alineado.png")));  // para mirarla
            const long alignedPixels = countPixels(yellow);
            expect(alignedPixels > 30, QStringLiteral("no se dibuja la recta de alineación en la marca (%1 píxeles amarillos)").arg(alignedPixels));
            blocking(QStringLiteral("alejarse un poco de la marca"), [&] { emit window_.viewport_->gizmoDragged(2, 51.0 * std::numbers::pi / 180.0); });
            expect(std::abs(rz->value() - 45.0) < 0.01, QStringLiteral("el giro no se quedó pegado a la marca al alejarse 6° (%1)").arg(rz->value()));
            if (current_ != nullptr) {
                current_->detail += QStringLiteral(" recta de alineación: %1 px").arg(alignedPixels);
            }
            // Cuánto cuesta cada paso de un arrastre de anillo (señal + dibujar el cuadro), como con el ratón real.
            {
                long slotTotal = 0;
                long paintTotal = 0;
                long worstStep = 0;
                constexpr int kSteps = 40;
                for (int i = 1; i <= kSteps; ++i) {
                    QElapsedTimer slot;
                    slot.start();
                    emit window_.viewport_->gizmoDragged(2, (i * 3.0) * std::numbers::pi / 180.0);
                    const long slotMs = slot.elapsed();
                    QElapsedTimer paint;
                    paint.start();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);  // el cuadro que se dibuja
                    window_.viewport_->update();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    slotTotal += slotMs;
                    paintTotal += paint.elapsed();
                    worstStep = std::max<long>(worstStep, slotMs + paint.elapsed());
                }
                // Lo mismo sin arrastrar: lo que cuesta dibujar la vista sola.
                long idleTotal = 0;
                for (int i = 0; i < kSteps; ++i) {
                    QElapsedTimer idle;
                    idle.start();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    window_.viewport_->update();
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                    idleTotal += idle.elapsed();
                }
                // Con el panel de la herramienta abierto y sin tocar nada, la vista no debe repintarse sin parar: un bucle de
                // layout en los cuadros numéricos llegó a repintar el panel cientos de veces por segundo y dejaba la vista a
                // unas 9 imágenes por segundo (un cuadro de 110 ms en vez de 16).
                {
                    struct Counter : QObject {
                        int paints = 0;
                        bool eventFilter(QObject*, QEvent* event) override
                        {
                            paints += event->type() == QEvent::Paint ? 1 : 0;
                            return false;
                        }
                    } counter;
                    QList<QWidget*> watched = window_.propertiesDock_->widget()->findChildren<QWidget*>();
                    watched.append(window_.propertiesDock_->widget());
                    for (QWidget* child : watched) {
                        child->installEventFilter(&counter);
                    }
                    settle(1000);
                    for (QWidget* child : watched) {
                        child->removeEventFilter(&counter);
                    }
                    expect(counter.paints < 200, QStringLiteral("el panel se repinta sin parar (%1 pinturas en 1 s sin tocar nada)").arg(counter.paints));
                    long idleFrame = 0;
                    for (int i = 0; i < 20; ++i) {
                        QElapsedTimer one;
                        one.start();
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                        window_.viewport_->update();
                        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                        idleFrame += one.elapsed();
                    }
                    idleFrame /= 20;
                    expect(idleFrame < 60, QStringLiteral("un cuadro de la vista con el panel abierto tarda %1 ms").arg(idleFrame));
                    expect(paintTotal / kSteps < 200, QStringLiteral("un paso de arrastre tarda %1 ms en dibujarse").arg(paintTotal / kSteps));
                    if (current_ != nullptr) {
                        current_->detail += QStringLiteral(" arrastre: gestión %1 ms + dibujo %2 ms por paso (peor %3 ms); cuadro sin arrastrar %4 ms; %5 pinturas del panel en 1 s")
                                                .arg(slotTotal / kSteps).arg(paintTotal / kSteps).arg(worstStep).arg(idleFrame).arg(counter.paints);
                    }
                }
                // Vuelve a 60° para lo que sigue.
                emit window_.viewport_->gizmoDragged(2, 43.5 * std::numbers::pi / 180.0);
            }
            QElapsedTimer dragStep;
            dragStep.start();
            blocking(QStringLiteral("seguir arrastrando el anillo Z"),
                     [&] { emit window_.viewport_->gizmoDragged(2, 60.0 * std::numbers::pi / 180.0); });
            settle(60);  // dibuja el cuadro con el arco del ángulo
            expect(dragStep.elapsed() < 500, QStringLiteral("un paso de arrastre del anillo (con el arco) tardó %1 ms").arg(dragStep.elapsed()));
            expect(std::abs(rz->value() - 60.0) < 0.11, QStringLiteral("fuera de las marcas el giro debía ser libre (%1)").arg(rz->value()));
            settle(200);
            const long leftoverLine = countPixels(yellow);
            expect(leftoverLine < 10, QStringLiteral("la recta de alineación sigue dibujada fuera de las marcas (%1 píxeles)").arg(leftoverLine));
            blocking(QStringLiteral("soltar el anillo Z"), [&] { emit window_.viewport_->gizmoReleased(2); });
            waitIdle();
            settle(250);
            const long redAfter = countPixels(ringRed);
            expect(redAfter > redBefore * 0.8, QStringLiteral("los otros anillos no volvieron al soltar (rojo %1, antes %2)").arg(redAfter).arg(redBefore));
            // Después de girar en Z, el anillo X gira sobre el eje X del MUNDO (el que dibuja el anillo), no sobre el de
            // la pieza ya girada: la orientación debe ser Rx(30°)·Rz(60°).
            blocking(QStringLiteral("arrastrar el anillo X"), [&] {
                emit window_.viewport_->gizmoPressed(0);
                emit window_.viewport_->gizmoDragged(0, 30.0 * std::numbers::pi / 180.0);
            });
            const std::array<QDoubleSpinBox*, 3> turns{spin(3), spin(4), spin(5)};
            if (turns[0] != nullptr && turns[1] != nullptr && turns[2] != nullptr) {
                const double deg = std::numbers::pi / 180.0;
                const auto got = nuf::app::fromTurns(turns[0]->value() * deg, turns[1]->value() * deg, turns[2]->value() * deg);
                const auto wanted = nuf::app::multiply(nuf::app::rotationAbout(0, 30.0 * deg), nuf::app::rotationAbout(2, 60.0 * deg));
                double worst = 0.0;
                for (std::size_t i = 0; i < 9; ++i) {
                    worst = std::max(worst, std::abs(got[i] - wanted[i]));
                }
                expect(worst < 0.005, QStringLiteral("el anillo X no giró sobre el eje X del mundo (diferencia %1; giros %2 %3 %4)")
                                          .arg(worst).arg(turns[0]->value()).arg(turns[1]->value()).arg(turns[2]->value()));
            }
            blocking(QStringLiteral("soltar el anillo X"), [&] { emit window_.viewport_->gizmoReleased(0); });
            waitIdle();
            // Se vuelve a la orientación de antes (solo Z = 60°) para el resto de la comprobación.
            if (turns[0] != nullptr && turns[1] != nullptr && turns[2] != nullptr) {
                blocking(QStringLiteral("deshacer el giro en X"), [&] {
                    turns[0]->setValue(0.0);
                    turns[1]->setValue(0.0);
                    turns[2]->setValue(60.0);
                });
                waitIdle();
            }
            expect(window_.tool_->previewing() == false, QStringLiteral("la vista previa no terminó al soltar"));
            if (QPushButton* ok = button(QStringLiteral("Aceptar")); ok != nullptr && ok->isEnabled()) {
                blocking(QStringLiteral("aceptar"), [&] { ok->click(); });
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
            waitIdle();
            const auto* move = window_.document_.featureCount() >= 2
                                   ? dynamic_cast<const document::MoveFeature*>(&window_.document_.feature(1))
                                   : nullptr;
            expect(move != nullptr, QStringLiteral("no se creó la operación Mover"));
            if (move != nullptr) {
                expect(std::abs(move->translation.x - 12.5) < 0.01, QStringLiteral("el desplazamiento guardado es %1").arg(move->translation.x));
                expect(std::abs(move->rotation.z - 60.0 * std::numbers::pi / 180.0) < 0.003,
                       QStringLiteral("el giro guardado es %1 rad").arg(move->rotation.z));
            }
        }
        if (window_.tool_ != nullptr) {
            if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
                cancel->click();
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        }
        undoAll();
    });

    // Un letrero con el texto «en otro cuerpo» son dos cuerpos (placa y texto): mover con los dos marcados los lleva juntos.
    step(QStringLiteral("Mover los dos cuerpos de un letrero a la vez"), [&] {
        QAction* sign = nullptr;
        for (QAction* action : generators) {
            if (action->text().contains(QStringLiteral("Letrero"))) {
                sign = action;
            }
        }
        expect(sign != nullptr, QStringLiteral("no hay generador Lightbox"));
        if (sign == nullptr) {
            return;
        }
        // Por defecto el texto va unido a la placa (un cuerpo): aquí se elige «en otro cuerpo». Se usa el letrero (pieza
        // pequeña) y no un lightbox para que la vista no quede lejos y el resto de la prueba parta de la de siempre.
        expect(openChangeAccept(sign, 0, QStringLiteral("En relieve (otro cuerpo)")) >= 1, QStringLiteral("no se creó el letrero"));
        const auto centers = [&] {
            std::vector<double> xs;
            for (const document::Body& body : window_.document_.result().bodies) {
                const geometry::Box box = body.shape.bounds();
                xs.push_back(0.5 * (box.min.x + box.max.x));
            }
            return xs;
        };
        const std::vector<double> before = centers();
        expect(before.size() >= 2, QStringLiteral("el letrero debía tener al menos dos cuerpos (%1)").arg(before.size()));
        blocking(QStringLiteral("abrir Mover"), [&] { window_.moveAction_->trigger(); });
        waitIdle();
        auto* list = window_.propertiesDock_->widget() != nullptr ? window_.propertiesDock_->widget()->findChild<QListWidget*>() : nullptr;
        expect(list != nullptr && list->count() == int(before.size()), QStringLiteral("la lista de cuerpos de Mover no coincide"));
        if (list != nullptr && window_.tool_ != nullptr) {
            blocking(QStringLiteral("marcar todos los cuerpos"), [&] {
                for (int i = 0; i < list->count(); ++i) {
                    list->item(i)->setCheckState(Qt::Checked);
                }
            });
            waitIdle();
            blocking(QStringLiteral("arrastrar la flecha X"), [&] {
                emit window_.viewport_->gizmoPressed(0);
                emit window_.viewport_->gizmoDragged(0, 15.0);
                emit window_.viewport_->gizmoReleased(0);
            });
            waitIdle();
            if (QPushButton* ok = button(QStringLiteral("Aceptar")); ok != nullptr && ok->isEnabled()) {
                blocking(QStringLiteral("aceptar"), [&] { ok->click(); });
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
            waitIdle();
            const std::vector<double> after = centers();
            expect(after.size() == before.size(), QStringLiteral("cambió el número de cuerpos"));
            for (std::size_t i = 0; i < std::min(before.size(), after.size()); ++i) {
                expect(std::abs(after[i] - before[i] - 15.0) < 0.1,
                       QStringLiteral("el cuerpo %1 se movió %2 mm en vez de 15").arg(i + 1).arg(after[i] - before[i]));
            }
        }
        if (window_.tool_ != nullptr) {
            if (QPushButton* cancel = button(QStringLiteral("Cancelar"))) {
                cancel->click();
            }
            waitFor([this] { return window_.tool_ == nullptr; }, 10000);
        }
        undoAll();
    });

    // Cota de un clic, al estilo Onshape: la figura se dibuja con clics (sin cota), la herramienta Cota la acota con un
    // clic y el valor se escribe en un campo sobre el lienzo (no en un diálogo); un clic en la cota la reabre.
    step(QStringLiteral("Cota de un clic con el valor escrito sobre el lienzo"), [&] {
        blocking(QStringLiteral("abrir un sketch"), [&] {
            window_.startSketch(sketch::Plane::xy(), QStringLiteral("prueba"), {});
        });
        expect(window_.sketch_ != nullptr, QStringLiteral("no se abrió el sketch"));
        if (window_.sketch_ == nullptr) {
            return;
        }
        SketchSession* session = window_.sketch_.get();
        const auto click = [&](double x, double y) { emit window_.viewport_->sketchClicked(x, y, Qt::NoModifier); };
        const auto circleRadius = [&](std::size_t index) {
            std::size_t seen = 0;
            for (const sketch::Entity& e : session->sketch().entities()) {
                if (const auto* circle = std::get_if<sketch::Circle>(&e.geometry)) {
                    if (seen++ == index) {
                        return circle->radius;
                    }
                }
            }
            return -1.0;
        };
        const auto dimensionCount = [&] {
            long count = 0;
            for (const sketch::Constraint& c : session->sketch().constraints()) {
                count += sketch::isDimensional(c.type) ? 1 : 0;
            }
            return count;
        };
        // El campo que sale sobre el lienzo (un recuadro emergente junto al cursor): se espera a que aparezca y se le
        // escribe un valor (o se cancela con Esc), como haría el usuario.
        bool popupSeen = false;
        const auto answer = [&](double value, bool accept) {
            popupSeen = false;
            auto attempts = std::make_shared<int>(0);
            auto poll = std::make_shared<std::function<void()>>();
            *poll = [this, value, accept, attempts, poll, &popupSeen] {
                QWidget* popup = QApplication::activePopupWidget();
                if (popup == nullptr) {
                    if (++*attempts < 40) {
                        QTimer::singleShot(100, *poll);
                    }
                    return;
                }
                popupSeen = true;
                popup->grab().save(QDir::temp().filePath(QStringLiteral("cota-campo.png")));  // para mirarlo
                if (accept) {
                    if (auto* box = popup->findChild<QDoubleSpinBox*>()) {
                        box->setValue(value);
                    }
                }
                QKeyEvent key(QEvent::KeyPress, accept ? Qt::Key_Return : Qt::Key_Escape, Qt::NoModifier);
                QWidget* target = QApplication::focusWidget() != nullptr ? QApplication::focusWidget() : popup;
                QCoreApplication::sendEvent(target, &key);
            };
            QTimer::singleShot(100, *poll);
        };

        // 1. Un círculo con dos clics: no lleva cota.
        session->setTool(SketchSession::Tool::Circle);
        click(0.0, 0.0);
        click(10.0, 0.0);
        expect(std::abs(circleRadius(0) - 10.0) < 1e-6, QStringLiteral("el círculo no se dibujó con dos clics (radio %1)").arg(circleRadius(0)));
        expect(dimensionCount() == 0, QStringLiteral("un círculo dibujado con clics no debía llevar cota"));

        // 2. Herramienta Cota: un clic en el círculo y el valor se escribe sobre el lienzo.
        session->setTool(SketchSession::Tool::Dimension);
        answer(30.0, true);
        blocking(QStringLiteral("acotar con un clic"), [&] { click(10.0, 0.0); });
        expect(popupSeen, QStringLiteral("no salió el campo de valor sobre el lienzo"));
        expect(dimensionCount() == 1, QStringLiteral("el clic no creó la cota (%1)").arg(dimensionCount()));
        expect(std::abs(circleRadius(0) - 15.0) < 1e-3, QStringLiteral("el diámetro escrito (30) no se aplicó (radio %1)").arg(circleRadius(0)));

        // 3. Un clic sobre la cota la reabre y la cambia; Esc la deja como estaba.
        const auto annotationOf = [&]() -> std::optional<sketch::Vec2> {
            for (const sketch::Annotation& note : window_.viewport_->sketchAnnotations(session->sketch())) {
                if (note.dimension) {
                    return note.anchor;
                }
            }
            return std::nullopt;
        };
        if (const auto at = annotationOf()) {
            answer(40.0, true);
            blocking(QStringLiteral("clic en la cota"), [&] { click(at->x, at->y); });
            expect(popupSeen, QStringLiteral("un clic en la cota no abrió su campo"));
            expect(std::abs(circleRadius(0) - 20.0) < 1e-3, QStringLiteral("la cota editada (40) no se aplicó (radio %1)").arg(circleRadius(0)));
            if (const auto again = annotationOf()) {
                answer(99.0, false);
                blocking(QStringLiteral("cancelar con Esc"), [&] { click(again->x, again->y); });
                expect(std::abs(circleRadius(0) - 20.0) < 1e-3, QStringLiteral("Esc debía dejar la cota como estaba (radio %1)").arg(circleRadius(0)));
            }
        } else {
            expect(false, QStringLiteral("la cota no tiene anotación"));
        }

        // 4. Dos círculos más con el mismo centro: sus cotas no se pisan.
        session->setTool(SketchSession::Tool::Circle);
        click(0.0, 0.0);
        click(21.0, 0.0);
        click(0.0, 0.0);
        click(22.0, 0.0);
        session->setTool(SketchSession::Tool::Dimension);
        answer(42.0, true);
        blocking(QStringLiteral("acotar el segundo círculo"), [&] { click(0.0, 21.0); });
        answer(44.0, true);
        blocking(QStringLiteral("acotar el tercer círculo"), [&] { click(0.0, 22.0); });
        std::vector<sketch::Vec2> anchors;
        for (const sketch::Annotation& note : window_.viewport_->sketchAnnotations(session->sketch())) {
            if (note.dimension) {
                anchors.push_back(note.anchor);
            }
        }
        expect(anchors.size() == 3, QStringLiteral("debía haber tres cotas (%1)").arg(anchors.size()));
        if (anchors.size() == 3) {
            const auto gap = [&](std::size_t i, std::size_t j) { return std::hypot(anchors[i].x - anchors[j].x, anchors[i].y - anchors[j].y); };
            expect(gap(0, 1) > 5.0 && gap(1, 2) > 5.0 && gap(0, 2) > 5.0,
                   QStringLiteral("las cotas de círculos concéntricos se pisan (%1, %2, %3)").arg(gap(0, 1)).arg(gap(1, 2)).arg(gap(0, 2)));
        }

        session->setTool(SketchSession::Tool::None);
        settle(300);
        window_.viewport_->grabFramebuffer().save(QDir::temp().filePath(QStringLiteral("cotas-concentricas.png")));  // para mirarlo
        blocking(QStringLiteral("terminar el sketch"), [&] { window_.finishSketch(); });
        waitIdle();
        undoAll();
    });

    // Inferencias al dibujar: con la herramienta Línea, llevar el cursor a la altura del origen dibuja la guía naranja
    // discontinua (se cuentan sus píxeles) y quita el enderezado; con Mayús suprimida no habría guía (no se prueba aquí).
    step(QStringLiteral("Guía naranja de alineación al dibujar"), [&] {
        blocking(QStringLiteral("abrir un sketch"), [&] {
            window_.startSketch(sketch::Plane::xy(), QStringLiteral("prueba"), {});
        });
        expect(window_.sketch_ != nullptr, QStringLiteral("no se abrió el sketch"));
        if (window_.sketch_ == nullptr) {
            return;
        }
        SketchSession* session = window_.sketch_.get();
        const auto countOrange = [&] {
            const QImage image = window_.viewport_->grabFramebuffer();
            long count = 0;
            for (int py = 0; py < image.height(); ++py) {
                for (int px = 0; px < image.width(); ++px) {
                    const QColor c = image.pixelColor(px, py);
                    count += (c.red() > 200 && c.green() > 120 && c.green() < 200 && c.blue() < 120) ? 1 : 0;
                }
            }
            return count;
        };
        session->setTool(SketchSession::Tool::Line);
        settle(300);
        const long before = countOrange();
        const double reach = 20.0;
        blocking(QStringLiteral("mover el cursor a la altura del origen"), [&] {
            emit window_.viewport_->sketchCursorMoved(true, reach, 0.02);
        });
        settle(300);
        const long during = countOrange();
        expect(during > before + 25, QStringLiteral("no se ve la guía naranja de alineación (%1 → %2 píxeles naranjas)").arg(before).arg(during));
        if (current_ != nullptr) {
            current_->detail += QStringLiteral(" guía: %1 → %2 px").arg(before).arg(during);
        }
        // Al alejar el cursor la guía desaparece.
        blocking(QStringLiteral("alejar el cursor"), [&] { emit window_.viewport_->sketchCursorMoved(true, reach, 9.0); });
        settle(300);
        const long after = countOrange();
        expect(after < during - 25, QStringLiteral("la guía no desapareció al alejar el cursor (%1 → %2)").arg(during).arg(after));
        session->setTool(SketchSession::Tool::None);
        blocking(QStringLiteral("terminar el sketch"), [&] { window_.finishSketch(); });
        waitIdle();
        undoAll();
    });

    // Ayuda de las herramientas: todas explican qué hacen al dejar el ratón encima (tooltip con una línea de ayuda tras
    // el nombre), y el banner de la barra de estado está siempre y parpadea al cambiar el mensaje.
    step(QStringLiteral("Cada herramienta tiene su ayuda y la barra de estado destaca el mensaje"), [&] {
        std::vector<QString> without;
        for (const auto& [tool, action] : window_.toolActions_) {
            Q_UNUSED(tool);
            if (!action->toolTip().contains(QStringLiteral("<br>"))) {
                without.push_back(action->text());
            }
        }
        for (QAction* action : window_.constraintActions_) {
            if (!action->toolTip().contains(QStringLiteral("<br>"))) {
                without.push_back(action->text());
            }
        }
        expect(without.empty(), QStringLiteral("herramientas sin ayuda: %1").arg(without.empty() ? QString() : without.front()));
        // El banner está siempre (también sin mensaje) y, al cambiar el mensaje, parpadea y vuelve al azul base.
        const auto banner = [&] { return window_.statusBar()->styleSheet(); };
        expect(banner().contains(QStringLiteral("min-height: 26px")), QStringLiteral("la barra de estado no tiene banner sin mensaje"));
        window_.statusBar()->showMessage(QStringLiteral("Prueba"));
        expect(banner().contains(QStringLiteral("min-height: 26px")), QStringLiteral("la barra de estado pierde el banner con un mensaje"));
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 1300) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        expect(banner().contains(QStringLiteral("background: #0b5cad")), QStringLiteral("el banner no vuelve al azul base tras parpadear"));
        window_.statusBar()->clearMessage();
        expect(banner().contains(QStringLiteral("min-height: 26px")), QStringLiteral("el banner desaparece al borrar el mensaje"));
    });

    // Sección y medición con un cuerpo.
    step(QStringLiteral("Sección y medición con un cuerpo"), [&] {
        expect(openChangeAccept(window_.boxAction_, 3) == 1, QStringLiteral("no se creó la caja"));
        waitIdle();
        // La malla que prepara el visor aparte debe llevar el borde de cada cara; sin él las caras del mismo color se funden.
        expect(!window_.document_.result().bodies.empty() &&
                   geometry::hasBoundaryPolygons(window_.document_.result().bodies.front().shape),
               QStringLiteral("el cuerpo se dibuja sin el borde de sus caras"));
        if (const QString folder = qEnvironmentVariable("NUF_PRUEBA_CAPTURAS"); !folder.isEmpty()) {
            window_.viewport_->grabFramebuffer().save(QDir(folder).filePath(QStringLiteral("cuerpo-solido.png")));  // para mirar las aristas
        }
        for (QAction* action : {window_.sectionAction_, window_.measureAction_}) {
            expect(action->isEnabled(), QStringLiteral("«%1» no está disponible con un cuerpo").arg(action->text()));
            blocking(QStringLiteral("activar"), [&] { action->trigger(); });
            expect(action->isChecked(), QStringLiteral("«%1» no se activó").arg(action->text()));
            waitIdle();
            blocking(QStringLiteral("desactivar"), [&] { action->trigger(); });
            expect(!action->isChecked(), QStringLiteral("«%1» no se desactivó").arg(action->text()));
        }
        undoAll();
    });

    // Un cilindro con una rosca cortante encima: cambiar el diámetro recalcula la rosca (lento), pero en segundo plano.
    step(QStringLiteral("Cambiar un cilindro con una rosca encima no cuelga la interfaz"), [&] {
        auto cylinder = std::make_unique<document::CylinderFeature>();
        cylinder->base.origin = document::OriginPlane::XY;
        cylinder->diameter = 10.0;
        cylinder->height = 10.0;
        auto thread = std::make_unique<document::ThreadFeature>();
        thread->base.origin = document::OriginPlane::XY;
        thread->spec.diameter = 8.0;
        thread->spec.pitch = 1.25;
        thread->spec.length = 10.0;
        thread->operation = document::Operation::Cut;
        blocking(QStringLiteral("agregar el cilindro y la rosca"), [&] {
            window_.commands_.push(std::make_unique<commands::AddFeature>(std::move(cylinder)));
            window_.commands_.push(std::make_unique<commands::AddFeature>(std::move(thread)));
        });
        waitIdle();
        const document::FeatureId cylinderId = window_.document_.feature(0).id();
        const document::FeatureId threadId = window_.document_.feature(1).id();
        expect(window_.document_.status(threadId).health != document::Health::Error,
               QStringLiteral("la rosca falló: %1").arg(QString::fromStdString(window_.document_.status(threadId).message)));
        for (const double diameter : {20.0, 10.0}) {
            auto edited = std::make_unique<document::CylinderFeature>(
                static_cast<const document::CylinderFeature&>(window_.document_.feature(0)));
            edited->diameter = diameter;
            QElapsedTimer timer;
            timer.start();
            blocking(QStringLiteral("cambiar el diámetro a %1").arg(diameter),
                     [&] { window_.commands_.push(std::make_unique<commands::EditFeature>(cylinderId, std::move(edited))); });
            expect(timer.elapsed() < 1000, QStringLiteral("cambiar el diámetro tardó %1 ms: debía volver al instante").arg(timer.elapsed()));
            expect(window_.document_.computing(), QStringLiteral("el recálculo no quedó en segundo plano"));
            // Mientras algo carga, la barra de estado se pone ámbar y gira un spinner (en la barra y sobre la vista).
            expect(window_.statusBar()->styleSheet().contains(QStringLiteral("background: #a85a00")),
                   QStringLiteral("el banner no se puso ámbar mientras calcula"));
            expect(window_.statusSpinner_ != nullptr && window_.statusSpinner_->isVisible(),
                   QStringLiteral("no se ve el spinner de la barra de estado mientras calcula"));
            expect(window_.badgeSpinner_ != nullptr && window_.badgeSpinner_->isVisible(),
                   QStringLiteral("no se ve el spinner sobre la vista mientras calcula"));
            if (const QString folder = qEnvironmentVariable("NUF_PRUEBA_CAPTURAS"); !folder.isEmpty()) {
                settle(300);  // que el spinner haya girado y el banner no esté a medio parpadeo
                window_.grab().save(QDir(folder).filePath(QStringLiteral("cargando.png")));
            }
            settle(100);  // la interfaz sigue atendiendo eventos mientras se calcula
        }
        waitIdle();
        expect(window_.document_.status(threadId).health != document::Health::Error, QStringLiteral("la rosca falló al final"));
        expect(window_.statusBar()->styleSheet().contains(QStringLiteral("background: #0b5cad")),
               QStringLiteral("el banner no vuelve al azul al terminar de calcular"));
        expect(window_.statusSpinner_ != nullptr && !window_.statusSpinner_->isVisible(),
               QStringLiteral("el spinner de la barra de estado sigue girando al terminar"));
        undoAll();
    });

    // --- Pasos de la auditoría de fluidez (F1) ----------------------------------------------------------------------
    // Reproducen lo que hoy se hace en el hilo de la interfaz y puede tardar (ver docs/auditorias/auditoria-3-fluidez.md).
    // Solo fallan por el umbral general: su fin es MEDIR antes de cambiar nada; las llamadas que pasen de 100 ms se
    // anotan en el informe con su tiempo.
    const auto note = [&](const QString& text) {
        if (current_ != nullptr) {
            current_->detail += (current_->detail.isEmpty() ? QString() : QStringLiteral("; ")) + text;
        }
    };
    const auto timed = [&](const QString& what, const std::function<void()>& call) {
        QElapsedTimer timer;
        timer.start();
        blocking(what, call);
        if (timer.elapsed() >= 100) {
            note(QStringLiteral("%1: %2 ms").arg(what).arg(timer.elapsed()));
        }
    };
    const auto add = [&](std::unique_ptr<document::Feature> feature) {
        blocking(QStringLiteral("agregar una operación"),
                 [&] { window_.commands_.push(std::make_unique<commands::AddFeature>(std::move(feature))); });
    };
    // Un cilindro con una rosca cortante (el cuerpo «pesado» de las pruebas).
    const auto addThreadedCylinder = [&] {
        auto cylinder = std::make_unique<document::CylinderFeature>();
        cylinder->base.origin = document::OriginPlane::XY;
        cylinder->diameter = 10.0;
        cylinder->height = 10.0;
        auto thread = std::make_unique<document::ThreadFeature>();
        thread->base.origin = document::OriginPlane::XY;
        thread->spec.diameter = 8.0;
        thread->spec.pitch = 1.25;
        thread->spec.length = 10.0;
        thread->operation = document::Operation::Cut;
        add(std::move(cylinder));
        add(std::move(thread));
    };
    const auto addBox = [&](double x) {
        auto box = std::make_unique<document::BoxFeature>();
        box->base.origin = document::OriginPlane::XY;
        box->x = x;
        add(std::move(box));
    };
    const QString scratch = QDir::tempPath() + QStringLiteral("/NufioCAD-prueba");
    QDir().mkpath(scratch);
    const auto scratchPath = [&](const QString& name) { return std::filesystem::path(QDir(scratch).filePath(name).toStdU16String()); };

    // Exportar, el diálogo de Exportar e importar un STEP ya van en segundo plano (Fase 13, F2): estos pasos usan el
    // mismo camino que la ventana y comprueban que lanzarlos devuelve al instante.
    step(QStringLiteral("Exportar STL, OBJ y STEP de una pieza con rosca (Archivo › Exportar, en segundo plano)"), [&] {
        addThreadedCylinder();
        waitIdle();
        std::vector<geometry::Shape> shapes;
        for (const document::Body& body : window_.document_.result().bodies) {
            shapes.push_back(body.shape);
        }
        expect(!shapes.empty(), QStringLiteral("no hay cuerpos que exportar"));
        const geometry::MeshQuality quality = geometry::MeshQuality::medium();
        const std::vector<std::pair<QString, ExportDialog::Format>> formats = {
            {QStringLiteral("exportar.stl"), ExportDialog::Format::Stl},
            {QStringLiteral("exportar.obj"), ExportDialog::Format::Obj},
            {QStringLiteral("exportar.step"), ExportDialog::Format::Step}};
        for (const auto& [name, format] : formats) {
            const QString file = QDir(scratch).filePath(name);
            QFile::remove(file);
            QElapsedTimer total;
            total.start();
            timed(QStringLiteral("lanzar la exportación a %1").arg(name), [&] {
                window_.startExport({MainWindow::ExportJob{file, shapes}}, format, quality);
            });
            expect(window_.exportRun_.running(), QStringLiteral("la exportación a %1 no quedó en segundo plano").arg(name));
            waitIdle();
            note(QStringLiteral("%1 listo en %2 ms").arg(name).arg(total.elapsed()));
            expect(QFileInfo(file).size() > 0, QStringLiteral("%1 no se escribió").arg(name));
            QFile::remove(file);
        }
        undoAll();
    });

    step(QStringLiteral("Diálogo de Exportar con 5 cuerpos: abrir, marcar y cambiar la calidad"), [&] {
        addThreadedCylinder();
        for (const double x : {30.0, 60.0, 90.0, 120.0}) {
            addBox(x);
        }
        waitIdle();
        const std::vector<document::Body> bodies = window_.document_.result().bodies;  // el diálogo guarda una referencia
        expect(bodies.size() == 5, QStringLiteral("se esperaban 5 cuerpos y hay %1").arg(bodies.size()));
        std::vector<std::string> keys;
        for (const document::Body& body : bodies) {
            keys.push_back(body.key);
        }
        std::unique_ptr<ExportDialog> dialog;
        timed(QStringLiteral("abrir el diálogo"), [&] { dialog = std::make_unique<ExportDialog>(bodies, keys, &window_); });
        // El conteo de triángulos y la revisión para imprimir corren en segundo plano: se espera a que terminen.
        const auto working = [&] {
            for (const QLabel* label : dialog->findChildren<QLabel*>()) {
                if (label->text().startsWith(QStringLiteral("Contando")) || label->text().startsWith(QStringLiteral("Revisando"))) {
                    return true;
                }
            }
            return false;
        };
        const auto finishWork = [&](const QString& what) {
            lastAction_ = what;
            QElapsedTimer total;
            total.start();
            settle(300);
            expect(waitFor([&] { return !working(); }, 180000), QStringLiteral("el diálogo no terminó de contar y revisar"));
            note(QStringLiteral("%1 listo en %2 ms").arg(what).arg(total.elapsed()));
        };
        finishWork(QStringLiteral("el conteo y la revisión iniciales"));
        if (QListWidget* list = dialog->findChild<QListWidget*>()) {
            for (int row = 0; row < list->count(); ++row) {
                timed(QStringLiteral("quitar la marca de un cuerpo"), [&] { list->item(row)->setCheckState(Qt::Unchecked); });
                timed(QStringLiteral("volver a marcarlo"), [&] { list->item(row)->setCheckState(Qt::Checked); });
            }
        } else {
            expect(false, QStringLiteral("el diálogo no tiene lista de cuerpos"));
        }
        finishWork(QStringLiteral("el conteo tras marcar y desmarcar"));
        for (QComboBox* combo : dialog->findChildren<QComboBox*>()) {
            if (combo->count() == 3 && combo->itemText(0).startsWith(QStringLiteral("Baja"))) {
                timed(QStringLiteral("calidad Alta"), [&] { combo->setCurrentIndex(2); });
                finishWork(QStringLiteral("el conteo en calidad Alta"));
                timed(QStringLiteral("calidad Baja"), [&] { combo->setCurrentIndex(0); });
                finishWork(QStringLiteral("el conteo en calidad Baja"));
            }
        }
        dialog.reset();
        undoAll();
    });

    step(QStringLiteral("Importar un STEP con un engranaje (Archivo › Importar STEP, en segundo plano)"), [&] {
        auto gear = std::make_unique<document::GearFeature>();
        gear->base.origin = document::OriginPlane::XY;
        gear->spec.teeth = 20;
        gear->spec.module = 2.0;
        gear->spec.thickness = 10.0;
        add(std::move(gear));
        waitIdle();
        expect(window_.document_.result().bodies.size() == 1, QStringLiteral("el engranaje no salió"));
        const std::filesystem::path file = scratchPath(QStringLiteral("importar.step"));
        if (!window_.document_.result().bodies.empty()) {
            geometry::writeStep(file, {window_.document_.result().bodies.front().shape});
        }
        undoAll();
        bool started = false;
        QElapsedTimer total;
        total.start();
        timed(QStringLiteral("lanzar la importación"), [&] { started = window_.importStepFile(file); });
        expect(started, QStringLiteral("no empezó la importación del STEP"));
        expect(window_.importRun_.running(), QStringLiteral("la importación no quedó en segundo plano"));
        waitIdle();
        note(QStringLiteral("importado en %1 ms").arg(total.elapsed()));
        expect(window_.document_.result().bodies.size() == 1, QStringLiteral("el STEP importado no dio un cuerpo"));
        QFile::remove(QString::fromStdU16String(file.u16string()));
        undoAll();
    });

    step(QStringLiteral("Medir caras de una rosca (lo que se calcula en cada cambio de la selección)"), [&] {
        addThreadedCylinder();
        waitIdle();
        if (window_.document_.result().bodies.empty()) {
            expect(false, QStringLiteral("no hay cuerpo"));
        } else {
            const geometry::Shape shape = window_.document_.result().bodies.front().shape;
            const int faces = shape.faceCount();
            expect(faces >= 2, QStringLiteral("el cuerpo tiene %1 caras").arg(faces));
            const auto measureFaces = [&](const std::vector<int>& indexes) {
                std::vector<geometry::MeasureElement> elements;
                for (const int index : indexes) {
                    elements.push_back({shape, true, index});
                }
                timed(QStringLiteral("medir %1 cara(s)").arg(indexes.size()), [&] { geometry::measure(elements); });
            };
            if (faces >= 2) {
                measureFaces({0});
                measureFaces({faces - 1});
                measureFaces({0, faces - 1});
                measureFaces({faces / 2, faces - 1});
            }
        }
        undoAll();
    });

    step(QStringLiteral("Pantalla de inicio con 10 recientes (leer las miniaturas)"), [&] {
        addBox(0.0);
        waitIdle();
        std::string png;
        timed(QStringLiteral("tomar la miniatura"), [&] { png = window_.thumbnailPng(); });
        const std::filesystem::path first = scratchPath(QStringLiteral("reciente-0.nuf"));
        timed(QStringLiteral("guardar un diseño"), [&] { io::saveDocument(first, window_.document_, window_.currentView(), png); });
        RecentFiles::clear();
        QStringList files;
        for (int i = 0; i < RecentFiles::kMax; ++i) {
            const QString name = QDir(scratch).filePath(QStringLiteral("reciente-%1.nuf").arg(i));
            if (i > 0) {
                QFile::remove(name);
                QFile::copy(QString::fromStdU16String(first.u16string()), name);
            }
            files << name;
            RecentFiles::add(name);
        }
        expect(window_.startScreen_ != nullptr, QStringLiteral("la ventana no tiene pantalla de inicio"));
        if (window_.startScreen_ != nullptr) {
            timed(QStringLiteral("recargar la lista de recientes"), [&] { window_.startScreen_->reload(); });
            lastAction_ = QStringLiteral("las miniaturas de los recientes");
            settle(1500);  // las miniaturas se leen en segundo plano y llegan de una en una
        }
        RecentFiles::clear();
        for (const QString& name : files) {
            QFile::remove(name);
        }
        undoAll();
    });

    step(QStringLiteral("Patrón lineal de 50 copias de una caja"), [&] {
        addBox(0.0);
        waitIdle();
        expect(!window_.document_.result().bodies.empty(), QStringLiteral("no hay caja"));
        if (!window_.document_.result().bodies.empty()) {
            auto pattern = std::make_unique<document::PatternFeature>();
            pattern->body = window_.document_.result().bodies.front().key;
            pattern->count = 50;
            pattern->spacing = 15.0;
            pattern->operation = document::Operation::Join;
            QElapsedTimer total;
            total.start();
            add(std::move(pattern));
            waitIdle();
            note(QStringLiteral("calculado en segundo plano en %1 ms").arg(total.elapsed()));
            const document::FeatureStatus status = window_.document_.status(window_.document_.feature(1).id());
            expect(status.health != document::Health::Error, QStringLiteral("el patrón falló: %1").arg(QString::fromStdString(status.message)));
        }
        undoAll();
    });

    // Lo que cuesta dibujar un cuadro de la vista vacía (el coste de base de la vista, sin pieza ni herramienta).
    step(QStringLiteral("Coste de dibujar la vista vacía"), [&] {
        waitIdle();
        long total = 0;
        constexpr int kFrames = 30;
        for (int i = 0; i < kFrames; ++i) {
            QElapsedTimer frame;
            frame.start();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            window_.viewport_->update();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            total += frame.elapsed();
        }
        if (current_ != nullptr) {
            current_->detail += QStringLiteral(" cuadro de la vista vacía: %1 ms").arg(total / kFrames);
        }
    });

    step(QStringLiteral("Al final todo está limpio"), [&] {
        expect(window_.document_.featureCount() == 0, QStringLiteral("quedan operaciones"));
        expect(window_.commands_.isClean(), QStringLiteral("el diseño figura como modificado"));
        expect(window_.tool_ == nullptr, QStringLiteral("queda una herramienta abierta"));
    });

    // Informe.
    int failed = 0;
    qint64 worst = 0;
    QString text = QStringLiteral("Prueba de la interfaz — %1\nLímite de parada: %2 ms\n\n")
                       .arg(QDateTime::currentDateTime().toString(Qt::ISODate)).arg(thresholdMs_);
    for (const Result& r : results_) {
        failed += r.ok ? 0 : 1;
        worst = std::max(worst, r.blockedMs);
        text += QStringLiteral("%1  %2  (parada máx. %3 ms%4)%5\n")
                    .arg(r.ok ? QStringLiteral("OK   ") : QStringLiteral("FALLO"), r.name)
                    .arg(r.blockedMs)
                    .arg(r.blockedBy.isEmpty() ? QString() : QStringLiteral(", al ") + r.blockedBy)
                    .arg(r.detail.isEmpty() ? QString() : QStringLiteral("\n       → ") + r.detail);
    }
    {
        std::lock_guard<std::mutex> lock(samplesMutex_);
        if (!samples_.empty()) {
            text += QStringLiteral("\nParadas largas (por defecto más de un segundo; dónde estaba la interfaz):\n");
            int shown = 0;
            for (const auto& [index, stack] : samples_) {
                if (++shown > 12) {
                    text += QStringLiteral("  … y %1 más\n").arg(samples_.size() - 12);
                    break;
                }
                const QString where = index >= 0 && index < int(results_.size()) ? results_[std::size_t(index)].name : QStringLiteral("(entre pasos)");
                text += QStringLiteral("  • en «%1»:\n%2").arg(where, stack);
            }
        }
    }
    text += QStringLiteral("\n%1 pasos, %2 fallidos; la parada más larga de la interfaz fue de %3 ms.\n")
                .arg(results_.size()).arg(failed).arg(worst);
    QFile file(reportPath_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);
        out << text;
    }
    return failed == 0 ? 0 : 1;
}

} // namespace nuf::app
