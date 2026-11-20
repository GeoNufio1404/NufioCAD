#include "MainWindow.hpp"

#include "ExportDialog.hpp"
#include "BodiesDialog.hpp"
#include "BusySpinner.hpp"
#include "StatusBanner.hpp"
#include "ExpressionSpinBox.hpp"
#include "RadialMenu.hpp"
#include "ValueInput.hpp"
#include "GeneratorTool.hpp"
#include "SettingsDialog.hpp"
#include "Units.hpp"
#include "ParametersDialog.hpp"
#include "FeatureTools.hpp"
#include "Icons.hpp"
#include "ModelPanels.hpp"
#include "NavBar.hpp"
#include "Recovery.hpp"
#include "StartScreen.hpp"
#include "Ribbon.hpp"
#include "Theme.hpp"

#include <nuf/commands/FeatureCommands.hpp>
#include <nuf/commands/RenameDocument.hpp>
#include <nuf/commands/SetBodyInfo.hpp>
#include <nuf/commands/SetParameters.hpp>
#include <nuf/geometry/DisplayUnits.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Kernel.hpp>
#include <nuf/commands/AddRecipe.hpp>
#include <nuf/commands/ReplaceRecipe.hpp>
#include <nuf/document/Generators.hpp>
#include <nuf/sketch/Annotations.hpp>
#include <nuf/geometry/Measure.hpp>
#include <nuf/io/NufFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QAction>
#include <QFileInfo>
#include <QFontMetrics>
#include <QDir>
#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QResizeEvent>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeySequence>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QSignalBlocker>
#include <QLineEdit>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QScrollArea>
#include <QSettings>
#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QStatusBar>
#include <QVariantAnimation>
#include <QToolBar>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>

#include "MainWindowCommon.hpp"

namespace nuf::app {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // La barra de estado propia (con el spinner de carga a la izquierda del mensaje) se pone antes de que nadie pida la normal.
    statusBanner_ = new StatusBanner(this);
    setStatusBar(statusBanner_);
    statusSpinner_ = statusBanner_->spinner();
    viewport_ = new render::Viewport(this);
    setCentralWidget(viewport_);
    // «Calculando…» sobre la vista, abajo a la izquierda (se recoloca al cambiar el tamaño de la vista).
    busyBadge_ = new QLabel(QStringLiteral("Calculando…"), viewport_);
    busyBadge_->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(20, 24, 31, 215); color: #E6EAF0; border: 1px solid #3A4250; border-radius: 12px;"
        " padding: 5px 14px 5px 36px; }"));  // el hueco de la izquierda es del spinner
    busyBadge_->adjustSize();
    badgeSpinner_ = new BusySpinner(busyBadge_, 16);
    badgeSpinner_->setColor(QColor(0xE6, 0xEA, 0xF0));
    badgeSpinner_->move(12, (busyBadge_->height() - badgeSpinner_->height()) / 2);
    // Botón «Cancelar» (U8): deja de esperar un cálculo largo volviendo al estado anterior (cierra la herramienta que
    // se está calculando o deshace el último cambio). Va dentro del recuadro, a la derecha del texto.
    {
        const int base = busyBadge_->sizeHint().width();
        auto* cancelButton = new QToolButton(busyBadge_);
        cancelButton->setObjectName(QStringLiteral("busyCancel"));
        cancelButton->setText(QStringLiteral("Cancelar"));
        cancelButton->setCursor(Qt::PointingHandCursor);
        cancelButton->setToolTip(QStringLiteral("Deja de calcular y vuelve al estado anterior (equivale a deshacer el último cambio)"));
        cancelButton->setStyleSheet(QStringLiteral(
            "QToolButton { background: rgba(240, 178, 90, 50); color: #F0B25A; border: 1px solid #F0B25A; border-radius: 9px;"
            " padding: 1px 8px; } QToolButton:hover { background: rgba(240, 178, 90, 110); }"));
        cancelButton->adjustSize();
        busyBadge_->setStyleSheet(busyBadge_->styleSheet().replace(QStringLiteral("padding: 5px 14px 5px 36px;"),
                                                                    QStringLiteral("padding: 5px %1px 5px 36px;").arg(cancelButton->width() + 22)));
        busyBadge_->adjustSize();
        cancelButton->move(base + 6 > 0 ? busyBadge_->width() - cancelButton->width() - 10 : 0,
                           (busyBadge_->height() - cancelButton->height()) / 2);
        connect(cancelButton, &QToolButton::clicked, this, [this] {
            if (tool_) {
                finishTool(false);
            } else if (commands_.canUndo()) {
                commands_.undo();
            }
            statusBar()->showMessage(QStringLiteral("Cálculo cancelado: se volvió al estado anterior"), 5000);
        });
    }
    busyBadge_->hide();
    viewport_->installEventFilter(this);
    createDocks();
    createMenus();

    auto* versions = new QLabel(QStringLiteral("OCCT %1 · SQLite %2 · Qt %3")
                                    .arg(QString::fromStdString(geometry::kernelVersion()),
                                         QString::fromStdString(io::NufFile::sqliteVersion()),
                                         QString::fromLatin1(qVersion())));
    sketchStatusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(sketchStatusLabel_);
    modelStatusLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(modelStatusLabel_);
    cursorLabel_ = new QLabel(this);
    cursorLabel_->setMinimumWidth(190);
    statusBar()->addPermanentWidget(cursorLabel_);
    // Selector de unidad de longitud (U1): el mismo ajuste que Configuración, a un clic. El modelo sigue en mm.
    unitBox_ = new QComboBox(this);
    unitBox_->setObjectName(QStringLiteral("unitBox"));
    unitBox_->setToolTip(QStringLiteral("Unidad con la que se muestran y se escriben las medidas (el modelo sigue en milímetros)"));
    for (const LengthUnit unit : {LengthUnit::Mm, LengthUnit::Cm, LengthUnit::M, LengthUnit::In}) {
        unitBox_->addItem(Units::label(unit), int(unit));
        unitBox_->setItemData(unitBox_->count() - 1, Units::name(unit), Qt::ToolTipRole);
    }
    unitBox_->setCurrentIndex(unitBox_->findData(int(Units::instance().length())));
    unitBox_->setFocusPolicy(Qt::NoFocus);
    connect(unitBox_, &QComboBox::activated, this, [this](int index) {
        Units::instance().setLength(LengthUnit(unitBox_->itemData(index).toInt()));
    });
    connect(&Units::instance(), &Units::changed, this, [this] {
        const QSignalBlocker block(unitBox_);
        unitBox_->setCurrentIndex(unitBox_->findData(int(Units::instance().length())));
    });
    statusBar()->addPermanentWidget(unitBox_);
    statusBar()->addPermanentWidget(versions);
    // La indicación de lo que se está haciendo (el mensaje de la barra) va en un banner de color: es donde está
    // lo importante y se olvida con facilidad. El banner está **siempre** (no aparece ni desaparece: con la barra
    // vacía sigue ahí, solo sin texto) y, cuando el mensaje cambia, parpadea una vez (se aclara y vuelve al azul
    // base) para llamar la atención sin saltos. Contraste de texto blanco sobre #0B5CAD: 6,6:1 (AA); el acento es
    // azul, como el de la selección, y no el rojo/ámbar de los errores.
    statusBar()->setSizeGripEnabled(false);
    paintBanner(QColor(0x0B, 0x5C, 0xAD));
    bannerBlink_ = new QVariantAnimation(this);
    bannerBlink_->setDuration(900);
    connect(bannerBlink_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) { paintBanner(value.value<QColor>()); });
    setBannerBusy(false);  // fija los colores del parpadeo
    auto lastMessage = std::make_shared<QString>();
    connect(statusBar(), &QStatusBar::messageChanged, this, [this, lastMessage](const QString& text) {
        if (!text.isEmpty() && text != *lastMessage) {
            bannerBlink_->stop();
            bannerBlink_->start();
        }
        *lastMessage = text;
    });
    connect(viewport_, &render::Viewport::selectionChanged, this,
            [this](const QString& description) { statusBar()->showMessage(description); });
    connect(viewport_, &render::Viewport::busyChanged, this, [this](bool busy) {
        viewBusy_ = busy;
        updateBusyBadge();
        if (busy) {
            statusBar()->showMessage(QStringLiteral("Preparando el dibujo de la pieza…"));
        } else {
            statusBar()->clearMessage();
        }
    });
    connect(viewport_, &render::Viewport::sketchPlanePicked, this, &MainWindow::startSketch);
    connect(viewport_, &render::Viewport::finishedSketchActivated, this, &MainWindow::editSketch);
    connect(timeline_, &TimelineWidget::featureActivated, this, &MainWindow::activateFeature);
    connect(timeline_, &TimelineWidget::featureMenuRequested, this, &MainWindow::showFeatureMenu);
    connect(timeline_, &TimelineWidget::markerMoved, this, [this](std::size_t marker) {
        if (!sketch_ && !tool_) {
            commands_.push(std::make_unique<commands::SetMarker>(marker));
        }
    });
    connect(timeline_, &TimelineWidget::featureMoved, this, [this](document::FeatureId id, std::size_t index) {
        if (sketch_ || tool_) {
            return;
        }
        if (!document_.canMoveFeature(id, index)) {
            statusBar()->showMessage(QStringLiteral("No se puede mover ahí: quedaría antes de lo que necesita o "
                                                    "después de lo que la usa"),
                                     6000);
            return;
        }
        commands_.push(std::make_unique<commands::MoveFeature>(id, index));
    });
    ExpressionSpinBox::setDocument(&document_);
    // Los generadores con texto piden el contorno de las letras a la vista (que es la que conoce las fuentes).
    document::setTextOutliner([this](const std::string& text, double height, const std::string& family) {
        return viewport_->textOutline(QString::fromStdString(text), height, QString::fromStdString(family));
    });
    // Las cotas del sketch se escriben en la unidad elegida.
    const auto applyUnits = [this] {
        sketch::setDimensionLengthScale(Units::instance().factor(), 3 + Units::instance().extraDecimals());
        geometry::display::setUnit(geometry::display::Unit(int(Units::instance().length())));  // Medir, avisos, resúmenes
        if (sketch_) {
            sketch_->refreshView();
        }
    };
    connect(&Units::instance(), &Units::changed, this, applyUnits);
    applyUnits();
    connect(navigator_, &NavigatorWidget::bodyVisibilityChanged, this, [this](const std::string& key, bool visible) {
        if (visible) {
            hiddenBodies_.erase(key);
        } else {
            hiddenBodies_.insert(key);
        }
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::planeVisibilityChanged, this, [this](document::FeatureId id, bool visible) {
        if (visible) {
            hiddenPlanes_.erase(id);
        } else {
            hiddenPlanes_.insert(id);
        }
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::componentVisibilityChanged, this, [this](const std::string& name, bool visible) {
        for (const document::Body& body : document_.result().bodies) {
            if (document_.componentOf(body.key) == name) {
                if (visible) {
                    hiddenBodies_.erase(body.key);
                } else {
                    hiddenBodies_.insert(body.key);
                }
            }
        }
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::sketchVisibilityChanged, this, [this](document::FeatureId id, bool visible) {
        sketchVisibility_[id] = visible;
        setViewDirty(true);
        refreshModel();
    });
    connect(navigator_, &NavigatorWidget::originVisibilityChanged, this, [this](bool visible) {
        originVisible_ = visible;
        setViewDirty(true);
        viewport_->setOriginVisible(visible);
    });
    connect(navigator_, &NavigatorWidget::sketchActivated, this, &MainWindow::editSketch);
    connectAssembly();
    connect(navigator_, &NavigatorWidget::groundVisibilityChanged, this, [this](bool visible) {
        groundVisible_ = visible;
        setViewDirty(true);
        viewport_->setGroundVisible(visible);
    });
    connect(viewport_, &render::Viewport::sketchCursorMoved, this, [this](bool hasPoint, double x, double y) {
        cursorLabel_->setText(hasPoint ? QStringLiteral("X %1   Y %2 %3")
                                             .arg(Units::instance().format(x), Units::instance().format(y), Units::instance().unit())
                                       : QString());
    });

    // El historial se recalcula en un hilo aparte: una rosca o una pieza grande no deben colgar la interfaz.
    document_.enableBackgroundCompute([this](std::function<void()> done) {
        QMetaObject::invokeMethod(this, std::move(done), Qt::QueuedConnection);
    });
    document_.computingChanged.connect([this](bool computing) {
        if (computing) {
            statusBar()->showMessage(QStringLiteral("Calculando el modelo… puedes seguir mirando; un cambio nuevo cancela el cálculo"));
        } else {
            statusBar()->clearMessage();
            auto pending = std::move(afterCompute_);
            afterCompute_.clear();
            for (const auto& what : pending) {
                what();
            }
        }
        updateSketchActions();
        updateExportAction();
        updateBusyBadge();
    });
    document_.changed.connect([this] {
        updateTitle();
        refreshModel();
    });
    commands_.changed.connect([this] {
        updateUndoActions();
        updateTitle();
    });
    setAcceptDrops(true);
    // Copia de recuperación cada minuto si hay cambios sin guardar.
    recovery_ = std::make_unique<Recovery>();
    bool secondsOk = false;
    const int seconds = qEnvironmentVariableIntValue("NUF_RECOVERY_SECONDS", &secondsOk);
    auto* recoveryTimer = new QTimer(this);
    recoveryTimer->setInterval((secondsOk && seconds > 0 ? seconds : 60) * 1000);
    connect(recoveryTimer, &QTimer::timeout, this, &MainWindow::saveRecovery);
    recoveryTimer->start();
    updateTitle();
    updateUndoActions();
    refreshModel();
    // Tamaño inicial: el de siempre, pero nunca más grande que la pantalla disponible (Fase 20, R2).
    {
        const QScreen* current = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
        const QRect available = current != nullptr ? current->availableGeometry() : QRect(0, 0, 1440, 900);
        resize(std::min(1440, int(available.width() * 0.95)), std::min(900, int(available.height() * 0.95)));
    }

    resizeDocks({navigatorDock_, propertiesDock_}, {240, 300}, Qt::Horizontal);
    // Recuerda tamaño de la ventana y distribución de paneles entre sesiones.
    defaultLayout_ = saveState(kLayoutVersion);
    const QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("ui/geometry")).toByteArray());
    // Si cambian las barras o los paneles, kLayoutVersion sube y la
    // distribución guardada (que ya no encaja) se descarta.
    restoreState(settings.value(QStringLiteral("ui/layout")).toByteArray(), kLayoutVersion);
    // La cinta siempre va en su propia fila, debajo de la barra de acceso rápido (con la ventana maximizada, una
    // disposición guardada las dejaba una al lado de la otra y la cinta se encogía).
    if (auto* ribbonBar = findChild<QToolBar*>(QStringLiteral("ribbonToolbar")); ribbonBar != nullptr && !toolBarBreak(ribbonBar)) {
        insertToolBarBreak(ribbonBar);
    }
    propertiesDock_->hide();  // aparece al usar una herramienta
    // La primera vez que se dibuja un símbolo (⛔ ⚠ ℹ ✓ …) Qt busca la fuente de respaldo entre todas las del sistema
    // (de 0,5 a 1,5 s en esta PC): se hace aquí, nada más abrir la ventana, y no al abrir el primer panel o generador
    // que lo use. Tiene que ser en el hilo de la interfaz (la base de fuentes de Qt no se toca desde otro).
    QTimer::singleShot(0, this, [] {
        const QFontMetrics metrics{QApplication::font()};
        metrics.horizontalAdvance(QStringLiteral("⛔⚠ℹ✓⏳→·…«»°²³"));
    });
}

MainWindow::~MainWindow()
{
    // Los hilos de cálculo avisan a esta ventana: se paran antes de que empiece a destruirse.
    document_.stopBackground();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == viewport_ && busyBadge_ != nullptr && event->type() == QEvent::Resize) {
        busyBadge_->move(16, viewport_->height() - busyBadge_->height() - 16);
    }
    if (watched == viewport_ && event->type() == QEvent::Resize) {
        placeRecoveryBar();
        if (startCard_ != nullptr && !startCardDismissed_) {
            updateStartCard();
        }
    }
    // Clic derecho sobre la vista (sin arrastrar) abre el menú radial; fuera de un comando, o dentro de un sketch.
    if (watched == viewport_ && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::RightButton) {
            if (event->type() == QEvent::MouseButtonPress) {
                // Solo con el botón derecho solo: con otro pulsado (girando con el izquierdo) no hay menú.
                const auto menus = findChildren<RadialMenu*>();
                const bool menuOpen = std::any_of(menus.begin(), menus.end(), [](const RadialMenu* m) { return m->isVisible(); });
                radialPending_ = !tool_ && !menuOpen && mouse->buttons() == Qt::RightButton;
                radialPress_ = mouse->position().toPoint();
            } else if (radialPending_) {
                radialPending_ = false;
                if (mouse->buttons() == Qt::NoButton && (mouse->position().toPoint() - radialPress_).manhattanLength() < 6) {
                    openRadialMenu(mouse->position().toPoint());
                }
            }
        }
    }
    // Con una herramienta abierta, escribir un número en la vista abre el cuadro del valor principal junto al cursor.
    if (watched == viewport_ && event->type() == QEvent::KeyPress && tool_ && !sketch_) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const QString text = key->text();
        const bool numeric = text.size() == 1 && (text.front().isDigit() || text == QLatin1String(".") ||
                                                  text == QLatin1String(",") || text == QLatin1String("-"));
        if (numeric && !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            startToolTyping(text);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::startToolTyping(const QString& text)
{
    if (!tool_) {
        return;
    }
    const FeatureTool::PrimaryField primary = tool_->primaryField();
    if (primary.box == nullptr) {
        return;
    }
    if (toolValueInput_ == nullptr) {
        toolValueInput_ = new ValueInput(viewport_);
        connect(toolValueInput_, &ValueInput::accepted, this, &MainWindow::applyToolValue);
    }
    const auto* expression = qobject_cast<ExpressionSpinBox*>(primary.box);
    const double shown = expression != nullptr && expression->isLength() ? Units::instance().toDisplay(primary.box->value())
                                                                          : primary.box->value();
    toolValueInput_->setFields({primary.label}, {shown});
    toolValueInput_->adjustSize();
    QPoint at = viewport_->mapFromGlobal(QCursor::pos()) + QPoint(16, 16);
    at.setX(std::clamp(at.x(), 0, std::max(0, viewport_->width() - toolValueInput_->width())));
    at.setY(std::clamp(at.y(), 0, std::max(0, viewport_->height() - toolValueInput_->height())));
    toolValueInput_->move(at);
    toolValueInput_->show();
    toolValueInput_->raise();
    toolValueInput_->startTyping(text);
}

void MainWindow::applyToolValue()
{
    if (!tool_ || toolValueInput_ == nullptr) {
        return;
    }
    const FeatureTool::PrimaryField primary = tool_->primaryField();
    const std::vector<std::optional<double>> typed = toolValueInput_->values();
    toolValueInput_->clear();
    toolValueInput_->hide();
    viewport_->setFocus();
    if (primary.box == nullptr || typed.empty() || !typed.front()) {
        return;
    }
    // Un número a secas se entiende en la unidad elegida (las longitudes) o tal cual (ángulos y cantidades).
    const auto* expression = qobject_cast<ExpressionSpinBox*>(primary.box);
    const double value = expression != nullptr && expression->isLength() ? Units::instance().fromDisplay(*typed.front()) : *typed.front();
    primary.box->setValue(value);
}

bool MainWindow::anythingBusy() const
{
    return toolComputing_ || document_.computing() || exportRun_.running() || importRun_.running() || measureRun_.running() ||
           viewBusy_;
}

// Colores del banner de la barra de estado: azul en reposo y ámbar mientras algo carga (blanco sobre #A85A00: 5,1:1).
void MainWindow::paintBanner(const QColor& background)
{
    statusBar()->setStyleSheet(
        QStringLiteral("QStatusBar { background: %1; color: white; font-weight: 600; min-height: 26px;"
                       " border-top: 2px solid %2; }"
                       "QStatusBar::item { border: 0; }"
                       "QStatusBar QLabel { color: white; font-weight: 400; }")
            .arg(background.name(), bannerBusy_ ? QStringLiteral("#F0B25A") : QStringLiteral("#4C9BEA")));
}

void MainWindow::setBannerBusy(bool busy)
{
    const QColor base = busy ? QColor(0xA8, 0x5A, 0x00) : QColor(0x0B, 0x5C, 0xAD);
    const QColor flash = busy ? QColor(0xD9, 0x83, 0x24) : QColor(0x4C, 0x9B, 0xEA);
    bannerBusy_ = busy;
    bannerBlink_->stop();
    bannerBlink_->setKeyValueAt(0.0, flash);
    bannerBlink_->setKeyValueAt(0.25, base);
    bannerBlink_->setKeyValueAt(0.4, flash);
    bannerBlink_->setKeyValueAt(1.0, base);
    paintBanner(base);
    if (statusBanner_ != nullptr) {
        statusBanner_->setBusy(busy);
    }
}

void MainWindow::updateBusyBadge()
{
    const bool busy = anythingBusy();
    if (busy != bannerBusy_ && bannerBlink_ != nullptr) {
        setBannerBusy(busy);
    }
    if (busyBadge_ == nullptr || busyBadge_->isVisible() == busy) {
        return;
    }
    busyBadge_->move(16, viewport_->height() - busyBadge_->height() - 16);
    busyBadge_->setVisible(busy);
    if (busy) {
        busyBadge_->raise();
    }
}

bool MainWindow::modelBusy()
{
    if (!document_.computing()) {
        return false;
    }
    statusBar()->showMessage(QStringLiteral("El modelo todavía se está calculando; espera a que termine"), 4000);
    return true;
}

void MainWindow::whenComputed(std::function<void()> what)
{
    if (document_.computing()) {
        afterCompute_.push_back(std::move(what));
    } else {
        what();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    adaptToSize();
}

void MainWindow::adaptToSize()
{
    if (navigatorDock_ == nullptr || timelineDock_ == nullptr || ribbon_ == nullptr) {
        return;
    }
    const bool narrow = width() < 1100;
    const bool low = height() < 640;
    if (narrow && !navigatorDock_->isHidden() && !navigatorAutoHidden_) {
        navigatorDock_->hide();
        navigatorAutoHidden_ = true;
    } else if (!narrow && navigatorAutoHidden_) {
        navigatorDock_->show();
        navigatorAutoHidden_ = false;
    }
    if (low && !timelineDock_->isHidden() && !timelineAutoHidden_) {
        timelineDock_->hide();
        timelineAutoHidden_ = true;
    } else if (!low && timelineAutoHidden_) {
        timelineDock_->show();
        timelineAutoHidden_ = false;
    }
    ribbon_->setCompact(low);
    // En una ventana baja la barra de acceso rápido se esconde (deshacer y guardar siguen en el menú y con sus teclas).
    if (auto* quick = findChild<QToolBar*>(QStringLiteral("quickToolbar"))) {
        quick->setVisible(!low);
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    recovery_->discard();  // cierre normal: no hace falta la copia
    QSettings settings;
    // Los paneles que se plegaron solos por el tamaño vuelven a guardarse como visibles.
    if (navigatorAutoHidden_) {
        navigatorDock_->show();
        navigatorAutoHidden_ = false;
    }
    if (timelineAutoHidden_) {
        timelineDock_->show();
        timelineAutoHidden_ = false;
    }
    settings.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("ui/layout"), saveState(kLayoutVersion));
    // La ventana desaparece ya: lo que quede calculando se cancela y se espera después, sin que parezca colgada.
    hide();
    QMainWindow::closeEvent(event);
}

void MainWindow::createDocks()
{
    // objectName: lo usa saveState()/restoreState() para recordar cada panel.
    navigatorDock_ = new QDockWidget(QStringLiteral("Navegador"), this);
    navigatorDock_->setObjectName(QStringLiteral("navigator"));
    navigator_ = new NavigatorWidget();
    navigatorDock_->setWidget(navigator_);
    navigatorDock_->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, navigatorDock_);

    propertiesDock_ = new QDockWidget(QStringLiteral("Propiedades"), this);
    propertiesDock_->setObjectName(QStringLiteral("properties"));
    propertiesPlaceholder_ = placeholder(QStringLiteral("Elige una herramienta (Extruir, Revolución…) para ver "
                                                         "sus propiedades aquí"));
    propertiesDock_->setWidget(propertiesPlaceholder_);
    propertiesDock_->setMinimumWidth(300);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);

    timelineDock_ = new QDockWidget(QStringLiteral("Timeline"), this);
    timelineDock_->setObjectName(QStringLiteral("timeline"));
    timeline_ = new TimelineWidget(document_);
    auto* scroll = new QScrollArea();
    scroll->setWidget(timeline_);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    // La línea de tiempo puede encoger (hasta una franja) si la ventana es baja; no crece más de lo que necesita.
    scroll->setMaximumHeight(timeline_->sizeHint().height() + 16);
    scroll->setMinimumHeight(56);
    // Controles de reproducción (U5): inicio, anterior, reproducir, siguiente y final; mueven el marcador del historial.
    auto* timelineBox = new QWidget();
    auto* timelineColumn = new QVBoxLayout(timelineBox);
    timelineColumn->setContentsMargins(0, 0, 0, 0);
    timelineColumn->setSpacing(0);
    auto* controls = new QHBoxLayout();
    controls->setContentsMargins(6, 2, 6, 0);
    controls->setSpacing(2);
    const auto control = [&](const QString& text, const QString& tip, const char* name) {
        auto* button = new QToolButton(timelineBox);
        button->setObjectName(QString::fromLatin1(name));
        button->setText(text);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        controls->addWidget(button);
        return button;
    };
    auto* toStart = control(QStringLiteral("⏮"), QStringLiteral("Marcador al principio del historial"), "timelineStart");
    auto* previous = control(QStringLiteral("◀"), QStringLiteral("Marcador un paso hacia atrás"), "timelinePrevious");
    timelinePlay_ = control(QStringLiteral("▶"), QStringLiteral("Reproducir el historial paso a paso desde el marcador"), "timelinePlay");
    auto* next = control(QStringLiteral("▶|"), QStringLiteral("Marcador un paso hacia delante"), "timelineNext");
    auto* toEnd = control(QStringLiteral("⏭"), QStringLiteral("Marcador al final del historial"), "timelineEnd");
    controls->addStretch();
    timelineColumn->addLayout(controls);
    timelineColumn->addWidget(scroll, 1);
    const auto moveMarker = [this](long target) {
        if (sketch_ || tool_) {
            return;
        }
        const long end = long(document_.featureCount());
        const long clamped = std::clamp(target, 0L, end);
        if (std::size_t(clamped) != document_.marker()) {
            commands_.push(std::make_unique<commands::SetMarker>(std::size_t(clamped)));
        }
    };
    connect(toStart, &QToolButton::clicked, this, [moveMarker] { moveMarker(0); });
    connect(previous, &QToolButton::clicked, this, [this, moveMarker] { moveMarker(long(document_.marker()) - 1); });
    connect(next, &QToolButton::clicked, this, [this, moveMarker] { moveMarker(long(document_.marker()) + 1); });
    connect(toEnd, &QToolButton::clicked, this, [this, moveMarker] { moveMarker(long(document_.featureCount())); });
    timelinePlayer_ = new QTimer(this);
    timelinePlayer_->setInterval(700);
    connect(timelinePlayer_, &QTimer::timeout, this, [this, moveMarker] {
        if (sketch_ || tool_ || document_.marker() >= document_.featureCount()) {
            timelinePlayer_->stop();
            timelinePlay_->setText(QStringLiteral("▶"));
            return;
        }
        moveMarker(long(document_.marker()) + 1);
    });
    connect(timelinePlay_, &QToolButton::clicked, this, [this, moveMarker] {
        if (timelinePlayer_->isActive()) {
            timelinePlayer_->stop();
            timelinePlay_->setText(QStringLiteral("▶"));
            return;
        }
        // Si el marcador ya está al final, se vuelve al principio para ver todo el historial.
        if (document_.marker() >= document_.featureCount()) {
            moveMarker(0);
        }
        timelinePlayer_->start();
        timelinePlay_->setText(QStringLiteral("⏸"));
    });
    timelineDock_->setWidget(timelineBox);
    timelineDock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    addDockWidget(Qt::BottomDockWidgetArea, timelineDock_);
}

void MainWindow::updateUndoActions()
{
    if (tool_) {
        undoAction_->setEnabled(false);
        redoAction_->setEnabled(false);
        return;
    }
    if (sketch_) {
        undoAction_->setEnabled(sketch_->canUndo());
        redoAction_->setEnabled(sketch_->canRedo());
        undoAction_->setText(QStringLiteral("Deshacer en el sketch"));
        redoAction_->setText(QStringLiteral("Rehacer en el sketch"));
        return;
    }
    undoAction_->setEnabled(commands_.canUndo());
    redoAction_->setEnabled(commands_.canRedo());
    undoAction_->setText(commands_.canUndo()
                             ? QStringLiteral("Deshacer %1").arg(QString::fromStdString(commands_.undoLabel()))
                             : QStringLiteral("Deshacer"));
    redoAction_->setText(commands_.canRedo()
                             ? QStringLiteral("Rehacer %1").arg(QString::fromStdString(commands_.redoLabel()))
                             : QStringLiteral("Rehacer"));
}

void MainWindow::updateTitle()
{
    // Con archivo, el título es su nombre (el que se ve al abrir y guardar);
    // sin él, el nombre interno del documento.
    const QString name = path_.empty() ? QString::fromStdString(document_.name())
                                       : QString::fromStdU16String(path_.stem().u16string());
    setWindowTitle(name + QStringLiteral("[*]"));
    setWindowModified(hasUnsavedWork());
    if (docTitle_ != nullptr) {
        docTitle_->setText(name.isEmpty() ? QStringLiteral("Sin título") : name + (hasUnsavedWork() ? QStringLiteral(" •") : QString()));
    }
    updateStartScreen();
}

// Como Fusion 360, la aplicación abre directo en un lienzo vacío. Los recientes
// están en un panel que se abre a petición (Archivo › Recientes…) y se cierra
// con cualquier acción o con Esc.
void MainWindow::updateStartScreen()
{
    if (startScreen_ == nullptr) {
        return;
    }
    if (isEditing() || viewport_->isPickingSketchPlane()) {
        recentsOpen_ = false;
    }
    startScreen_->setVisible(recentsOpen_);
}

void MainWindow::showRecents()
{
    if (isEditing() || viewport_->isPickingSketchPlane()) {
        return;
    }
    recentsOpen_ = true;
    updateStartScreen();
}

void MainWindow::createSketch()
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    viewport_->pickSketchPlane();
    statusBar()->showMessage(QStringLiteral("Elige un plano de origen o una cara plana · Esc para cancelar"));
    updateSketchActions();
}

void MainWindow::startSketch(const sketch::Plane& plane, const QString& description,
                             const document::SketchAttachment& attachment)
{
    editingSketch_.reset();
    newSketchAttachment_ = attachment;
    openSession(plane, description, std::nullopt);
}

void MainWindow::editSketch(document::FeatureId id)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    const auto index = document_.indexOf(id);
    const auto* feature = dynamic_cast<const document::SketchFeature*>(document_.findFeature(id));
    if (!index || feature == nullptr) {
        return;
    }
    // Su plano tal como quedó al calcularlo (sigue a su cara).
    const auto& after = document_.stateBefore(*index + 1).sketches;
    const auto placed = after.find(id);
    const sketch::Plane plane = placed != after.end() ? placed->second.plane : feature->attachment().lastPlane;
    // Con lo proyectado ya actualizado.
    // (y con las expresiones de las cotas, para poder editarlas)
    const sketch::Sketch sketch =
        feature->forEditing(placed != after.end() ? *placed->second.sketch : feature->sketch());
    editingSketch_ = id;
    refreshModel();  // el modelo como estaba antes de este sketch
    openSession(plane, QString::fromStdString(feature->name()), sketch);
}

void MainWindow::openSession(const sketch::Plane& plane, const QString& description,
                             std::optional<sketch::Sketch> initial)
{
    viewport_->clearModelSelection();
    sketch_ = std::make_unique<SketchSession>(*viewport_, plane, std::move(initial));
    updateStartCard();
    connect(sketch_.get(), &SketchSession::toolChanged, this, [this](SketchSession::Tool tool) {
        for (const auto& [kind, action] : toolActions_) {
            action->setChecked(kind == tool);
        }
    });
    connect(sketch_.get(), &SketchSession::hintChanged, this, [this, description](const QString& hint) {
        statusBar()->showMessage(hint.isEmpty() ? QStringLiteral("Sketch en %1").arg(description) : hint);
    });
    connect(sketch_.get(), &SketchSession::statusChanged, sketchStatusLabel_, &QLabel::setText);
    connect(sketch_.get(), &SketchSession::historyChanged, this, &MainWindow::updateUndoActions);
    connect(sketch_.get(), &SketchSession::selectionChanged, this, &MainWindow::updateSketchActions);
    connect(sketch_.get(), &SketchSession::message, this,
            [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    statusBar()->showMessage(QStringLiteral("Sketch en %1 · L línea, R rectángulo, C círculo, A arco").arg(description));
    sketch_->start();
    timeline_->setEditing(editingSketch_);
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::finishSketch()
{
    if (!sketch_) {
        return;
    }
    sketch::Sketch finished = sketch_->sketch();
    const sketch::Plane plane = sketch_->plane();
    sketch_.reset();
    updateStartCard();
    const std::optional<document::FeatureId> editing = std::exchange(editingSketch_, std::nullopt);
    timeline_->setEditing(std::nullopt);
    sketchStatusLabel_->clear();
    statusBar()->clearMessage();
    if (editing) {
        // Un solo paso de deshacer para toda la edición del sketch.
        if (const auto* feature = dynamic_cast<const document::SketchFeature*>(document_.findFeature(*editing))) {
            auto edited = std::unique_ptr<document::SketchFeature>(
                static_cast<document::SketchFeature*>(feature->clone().release()));
            edited->setSketchFromEditing(std::move(finished));
            const std::string label = "Editar " + feature->name();
            commands_.push(std::make_unique<commands::EditFeature>(*editing, std::move(edited), label));
        }
    } else {
        const bool empty =
            std::ranges::none_of(finished.entities(), [](const sketch::Entity& e) { return !e.reference; });
        if (!empty) {
            document::SketchAttachment attachment = newSketchAttachment_;
            attachment.lastPlane = plane;
            auto created = std::make_unique<document::SketchFeature>(sketch::Sketch(), attachment);
            created->setSketchFromEditing(std::move(finished));
            commands_.push(std::make_unique<commands::AddFeature>(std::move(created)));
        }
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

// --- Operaciones sólidas ---------------------------------------------------------

void MainWindow::startTool(std::unique_ptr<FeatureTool> tool)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    tool_ = std::move(tool);
    updateStartCard();
    connect(tool_.get(), &FeatureTool::accepted, this, [this] { finishTool(true); });
    connect(tool_.get(), &FeatureTool::cancelled, this, [this] { finishTool(false); });
    connect(tool_.get(), &FeatureTool::message, this, [this](const QString& text) { statusBar()->showMessage(text, 6000); });
    connect(tool_.get(), &FeatureTool::computingChanged, this, [this](bool computing) {
        toolComputing_ = computing;
        updateBusyBadge();
    });
    refreshModel();  // al editar: el modelo hasta esa operación
    tool_->start();
    propertiesDock_->setWidget(tool_->panel());
    propertiesDock_->show();
    // Opción «Comandos flotantes» (U5): el panel flota sobre el lienzo, arriba a la derecha, en vez de ir acoplado.
    if (QSettings().value(QStringLiteral("view/floatingCommands"), false).toBool()) {
        propertiesDock_->setFloating(true);
        const int width = std::min(std::max(propertiesDock_->width(), 340), std::max(300, viewport_->width() / 2));
        const int height = std::min(std::max(propertiesDock_->sizeHint().height(), 320), std::max(240, viewport_->height() - 40));
        propertiesDock_->resize(width, height);
        propertiesDock_->move(viewport_->mapToGlobal(QPoint(std::max(0, viewport_->width() - width - 16), 16)));
        // Si el usuario lo movió o le cambió el tamaño la vez anterior, vuelve a donde lo dejó (si aún cabe en alguna pantalla).
        const QRect saved = QSettings().value(QStringLiteral("view/floatingCommandsGeometry")).toRect();
        if (saved.isValid() && saved.width() >= 240 && saved.height() >= 200) {
            for (QScreen* screen : QGuiApplication::screens()) {
                if (screen->availableGeometry().contains(saved.topLeft() + QPoint(40, 20))) {
                    propertiesDock_->setGeometry(saved);
                    break;
                }
            }
        }
    }
    // Si hay sitio, el panel se abre del ancho que pide su contenido (hasta el 40 % de la ventana); si no, se desplaza.
    {
        const int wanted = tool_->panel()->sizeHint().width() + 24;
        const int width = std::clamp(wanted, 300, std::max(300, int(this->width() * 0.4)));
        if (propertiesDock_->width() < width) {
            resizeDocks({propertiesDock_}, {width}, Qt::Horizontal);
        }
    }
    timeline_->setEditing(tool_->editing());
    statusBar()->showMessage(QStringLiteral("Elige las entradas en la vista · Enter acepta · Esc cancela"));
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::startGenerator(const document::Generator& generator)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    startTool(std::make_unique<GeneratorTool>(document_, *viewport_, generator));
}

void MainWindow::reopenGenerator(document::FeatureId id)
{
    if (sketch_ || tool_ || viewport_->isPickingSketchPlane() || modelBusy()) {
        return;
    }
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || feature->origin.lead == 0) {
        return;
    }
    const document::FeatureId lead = feature->origin.lead;
    const std::vector<document::FeatureId> group = document::generatedGroup(document_, lead);
    const document::Generator* generator = group.empty() ? nullptr : document::findGenerator(document_.findFeature(group.front())->origin.generator);
    if (generator == nullptr) {
        statusBar()->showMessage(QStringLiteral("Este generador ya no existe en esta versión"), 8000);
        return;
    }
    const document::GeneratorOrigin& origin = document_.findFeature(group.front())->origin;
    startTool(std::make_unique<GeneratorTool>(document_, *viewport_, *generator, lead, document::decodeValues(origin.values),
                                              origin.suffix));
}

void MainWindow::finishTool(bool accepted)
{
    if (toolValueInput_ != nullptr) {
        toolValueInput_->clear();
        toolValueInput_->hide();
    }
    if (!tool_) {
        return;
    }
    // Un generador no da una operación sino una receta (parámetros, operaciones y nombres) que se aplica de una vez.
    std::unique_ptr<commands::Command> recipe;
    if (const auto* generator = dynamic_cast<const GeneratorTool*>(tool_.get()); generator != nullptr && accepted) {
        try {
            document::Recipe made = generator->recipe();
            if (generator->lead() != 0) {
                if (const std::string problem = commands::ReplaceRecipe::problem(document_, generator->lead(), made); !problem.empty()) {
                    statusBar()->showMessage(QString::fromStdString(problem), 12000);
                    return;  // la herramienta sigue abierta
                }
                recipe = std::make_unique<commands::ReplaceRecipe>(generator->lead(), std::move(made), generator->generator().title());
            } else {
                recipe = std::make_unique<commands::AddRecipe>(std::move(made), generator->generator().title());
            }
        } catch (const std::exception& e) {
            statusBar()->showMessage(QString::fromUtf8(e.what()), 8000);  // p. ej. un texto sin letras
            return;                                                       // la herramienta sigue abierta
        }
    }
    std::unique_ptr<document::Feature> feature = accepted && !recipe ? tool_->make() : nullptr;
    const std::optional<document::FeatureId> editing = tool_->editing();
    const document::FeatureId id = tool_->featureId();
    tool_->stopPreview();
    toolComputing_ = false;
    updateBusyBadge();
    // Se destruye después: esta llamada viene de una señal de la herramienta.
    if (propertiesDock_->isFloating() && propertiesDock_->isVisible()) {
        QSettings().setValue(QStringLiteral("view/floatingCommandsGeometry"), propertiesDock_->geometry());
    }
    propertiesDock_->setWidget(propertiesPlaceholder_);
    propertiesDock_->setFloating(false);
    propertiesDock_->hide();  // como los diálogos de Fusion: solo mientras hay un comando
    tool_.release()->deleteLater();
    updateStartCard();
    timeline_->setEditing(std::nullopt);
    statusBar()->clearMessage();
    viewport_->clearModelSelection();
    viewport_->clearToolPreview();
    viewport_->hideDistanceHandle();
    viewport_->setAdditiveSelection(false);
    if (recipe) {
        commands_.push(std::move(recipe));
    } else if (feature) {
        if (editing) {
            commands_.push(std::make_unique<commands::EditFeature>(*editing, std::move(feature)));
        } else {
            feature->setId(id);
            commands_.push(std::make_unique<commands::AddFeature>(std::move(feature)));
        }
    }
    refreshModel();
    updateSketchActions();
    updateUndoActions();
}

void MainWindow::activateFeature(document::FeatureId id)
{
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || sketch_ || tool_) {
        return;
    }
    switch (feature->kind()) {
    case document::FeatureKind::Sketch:
        editSketch(id);
        break;
    case document::FeatureKind::Extrude:
        startTool(std::make_unique<ExtrudeTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Revolve:
        startTool(std::make_unique<RevolveTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Fillet:
    case document::FeatureKind::Chamfer:
        startTool(std::make_unique<EdgeTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Shell:
        startTool(std::make_unique<ShellTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Hole:
        startTool(std::make_unique<HoleTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Mirror:
        startTool(std::make_unique<MirrorTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Split:
        startTool(std::make_unique<SplitTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Pattern:
        startTool(std::make_unique<PatternTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::ConstructionPlane:
        startTool(std::make_unique<PlaneTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::ConstructionAxis:
        startTool(std::make_unique<AxisTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Box:
        startTool(std::make_unique<BoxTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Cylinder:
        startTool(std::make_unique<CylinderTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Gear:
        startTool(std::make_unique<GearTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Thread:
        startTool(std::make_unique<ThreadTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Sweep:
        startTool(std::make_unique<SweepTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Loft:
        startTool(std::make_unique<LoftTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Move:
        startTool(std::make_unique<MoveTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Combine:
        startTool(std::make_unique<CombineTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Joint:
        startTool(std::make_unique<JointTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Align:
        startTool(std::make_unique<JointTool>(document_, *viewport_, id, true));
        break;
    case document::FeatureKind::MotionLink:
        startTool(std::make_unique<MotionLinkTool>(document_, *viewport_, id));
        break;
    case document::FeatureKind::Import:
        break;  // no tiene parámetros que editar
    }
}

void MainWindow::showFeatureMenu(document::FeatureId id, const QPoint& globalPos)
{
    const document::Feature* feature = document_.findFeature(id);
    if (feature == nullptr || sketch_ || tool_) {
        return;
    }
    const QString name = QString::fromStdString(feature->name());
    QMenu menu(this);
    menu.addAction(QStringLiteral("Editar"), this, [this, id] { activateFeature(id); });
    if (feature->origin.lead != 0) {
        if (const document::Generator* generator = document::findGenerator(feature->origin.generator)) {
            menu.addAction(QStringLiteral("Reabrir «%1»…").arg(QString::fromStdString(generator->title())), this,
                           [this, id] { reopenGenerator(id); });
        }
    }
    menu.addAction(QStringLiteral("Renombrar…"), this, [this, id, name] {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Renombrar"), QStringLiteral("Nombre:"),
                                                   QLineEdit::Normal, name, &ok);
        const document::Feature* f = document_.findFeature(id);
        if (ok && f && !text.trimmed().isEmpty() && text.trimmed() != name) {
            auto renamed = f->clone();
            renamed->setName(text.trimmed().toStdString());
            commands_.push(std::make_unique<commands::EditFeature>(
                id, std::move(renamed), "Renombrar " + name.toStdString()));
        }
    });
    const bool suppressed = feature->suppressed();
    menu.addAction(suppressed ? QStringLiteral("Reactivar") : QStringLiteral("Suprimir"), this, [this, id, suppressed] {
        commands_.push(std::make_unique<commands::SuppressFeature>(id, !suppressed));
    });
    menu.addAction(QStringLiteral("Mover el marcador aquí"), this, [this, id] {
        if (const auto index = document_.indexOf(id)) {
            commands_.push(std::make_unique<commands::SetMarker>(*index + 1));
        }
    });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Borrar"), this, [this, id, name] {
        const std::vector<document::FeatureId> users = document_.dependents(id);
        if (!users.empty()) {
            QStringList names;
            for (document::FeatureId user : users) {
                names << QString::fromStdString(document_.findFeature(user)->name());
            }
            const auto answer = QMessageBox::question(
                this, QStringLiteral("Borrar %1").arg(name),
                QStringLiteral("Estas operaciones dependen de %1 y quedarán con error:\n\n%2\n\n¿Borrarla igualmente?")
                    .arg(name, names.join(QStringLiteral(", "))));
            if (answer != QMessageBox::Yes) {
                return;
            }
        }
        commands_.push(std::make_unique<commands::DeleteFeature>(id));
    });
    menu.exec(globalPos);
}

} // namespace nuf::app
