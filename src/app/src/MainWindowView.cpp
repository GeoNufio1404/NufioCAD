#include "MainWindow.hpp"

#include "ExportDialog.hpp"
#include "BodiesDialog.hpp"
#include "ExpressionSpinBox.hpp"
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
#include <QDir>
#include <QApplication>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDockWidget>
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
#include <QStatusBar>
#include <QToolBar>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <tuple>
#include <utility>

#include "MainWindowCommon.hpp"

namespace nuf::app {

// --- Vista del modelo ----------------------------------------------------------------

std::size_t MainWindow::shownUpTo() const
{
    if (editingSketch_) {
        return document_.indexOf(*editingSketch_).value_or(document_.marker());
    }
    if (tool_) {
        return tool_->position();
    }
    return document_.marker();
}

bool MainWindow::sketchVisible(document::FeatureId id, std::size_t upTo) const
{
    if (const auto chosen = sketchVisibility_.find(id); chosen != sketchVisibility_.end()) {
        return chosen->second;
    }
    // Como en Fusion: un sketch se oculta cuando una operación ya lo usó.
    for (std::size_t i = 0; i < upTo; ++i) {
        const document::Feature& f = document_.feature(i);
        if (f.kind() == document::FeatureKind::Sketch || f.suppressed()) {
            continue;
        }
        const std::vector<document::FeatureId> deps = f.dependencies();
        if (std::ranges::find(deps, id) != deps.end()) {
            return false;
        }
    }
    return true;
}

void MainWindow::updateExportAction()
{
    updateStartScreen();
    for (QAction* action : {filletAction_, chamferAction_, shellAction_, holeAction_, mirrorAction_, patternAction_, moveAction_, combineAction_, organizeAction_, jointAction_}) {
        if (action != nullptr) {
            action->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane() && !document_.result().bodies.empty());
        }
    }
    for (QAction* action : generatorActions_) {
        action->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane());
    }
    if (sectionAction_ != nullptr) {
        if ((sketch_ || tool_) && sectionAction_->isChecked()) {
            sectionAction_->setChecked(false);
            toggleSection(false);
        }
        sectionAction_->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane() &&
                                   !document_.result().bodies.empty());
    }
    if (measureAction_ != nullptr) {
        if ((sketch_ || tool_) && measureAction_->isChecked()) {
            measureAction_->setChecked(false);
            toggleMeasure(false);
        }
        measureAction_->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane() &&
                                   !document_.result().bodies.empty());
    }
    for (QAction* action : {planeAction_, axisAction_, parametersAction_, gearAction_, threadAction_, boxAction_, cylinderAction_}) {
        if (action != nullptr) {
            action->setEnabled(!sketch_ && !tool_ && !viewport_->isPickingSketchPlane());
        }
    }
    if (importAction_ != nullptr) {
        importAction_->setEnabled(!sketch_ && !tool_);
    }
    if (exportAction_ != nullptr) {
        exportAction_->setEnabled(!sketch_ && !tool_ && !document_.result().bodies.empty());
    }
    if (printAction_ != nullptr) {
        printAction_->setEnabled(!sketch_ && !tool_ && !document_.result().bodies.empty());
    }
}

// Sección: corta la vista con un plano de origen (XY, XZ o YZ) a la posición elegida; solo se ve,
// el modelo no cambia.
void MainWindow::toggleSection(bool on)
{
    if (sectionPanel_ == nullptr) {
        sectionPanel_ = new QFrame(viewport_);
        sectionPanel_->setObjectName(QStringLiteral("sectionPanel"));
        sectionPanel_->setStyleSheet(QStringLiteral(
            "QFrame#sectionPanel { background: rgba(32, 36, 42, 225); border: 1px solid #4A515B; border-radius: 6px; } "
            "QFrame#sectionPanel QLabel, QFrame#sectionPanel QCheckBox { color: #E8EBEF; }"));
        auto* form = new QFormLayout(sectionPanel_);
        form->setContentsMargins(12, 8, 12, 8);
        auto* title = new QLabel(QStringLiteral("<b>Sección</b>"), sectionPanel_);
        form->addRow(title);
        sectionPlane_ = new QComboBox(sectionPanel_);
        sectionPlane_->addItems({QStringLiteral("Plano XY (horizontal)"), QStringLiteral("Plano XZ (frontal)"),
                                 QStringLiteral("Plano YZ (lateral)")});
        form->addRow(QStringLiteral("Corte"), sectionPlane_);
        sectionOffset_ = new ExpressionSpinBox(sectionPanel_);
        sectionOffset_->setRange(-100000.0, 100000.0);
        sectionOffset_->setDecimals(2);
        static_cast<ExpressionSpinBox*>(sectionOffset_)->setLength(true);
        form->addRow(QStringLiteral("Posición"), sectionOffset_);
        sectionFlip_ = new QCheckBox(QStringLiteral("Ver el otro lado"), sectionPanel_);
        form->addRow(QString(), sectionFlip_);
        connect(sectionPlane_, &QComboBox::currentIndexChanged, this, [this] {
            // Al cambiar de plano, el corte vuelve al centro del modelo en ese eje.
            const int axis = sectionPlane_->currentIndex() == 0 ? 2 : sectionPlane_->currentIndex() == 1 ? 1 : 0;
            double lo = 0.0;
            double hi = 0.0;
            bool first = true;
            for (const document::Body& body : document_.result().bodies) {
                const geometry::Box box = body.shape.bounds();
                if (box.empty) {
                    continue;
                }
                const double a = axis == 0 ? box.min.x : axis == 1 ? box.min.y : box.min.z;
                const double b = axis == 0 ? box.max.x : axis == 1 ? box.max.y : box.max.z;
                lo = first ? a : std::min(lo, a);
                hi = first ? b : std::max(hi, b);
                first = false;
            }
            sectionOffset_->setValue((lo + hi) / 2.0);
        });
        connect(sectionOffset_, &QDoubleSpinBox::valueChanged, this, [this] { updateSection(); });
        connect(sectionFlip_, &QCheckBox::toggled, this, [this] { updateSection(); });
        sectionPanel_->move(16, 16);
    }
    if (on) {
        // Fuerza la posición inicial (el centro del modelo) aunque el plano ya fuera el primero.
        QSignalBlocker block(sectionOffset_);
        emit sectionPlane_->currentIndexChanged(sectionPlane_->currentIndex());
        block.unblock();
        sectionPanel_->adjustSize();
        sectionPanel_->show();
        updateSection();
        statusBar()->showMessage(QStringLiteral("Sección: mueve el corte con la posición · Esc termina"));
    } else {
        sectionPanel_->hide();
        viewport_->clearSectionPlane();
        statusBar()->clearMessage();
    }
    if (measurePanel_ != nullptr && measurePanel_->isVisible()) {
        updateMeasure();
    }
}

void MainWindow::updateSection()
{
    if (sectionPlane_ == nullptr || sectionAction_ == nullptr || !sectionAction_->isChecked()) {
        return;
    }
    const int plane = sectionPlane_->currentIndex();
    // Por defecto se quita el lado que da a la cámara isométrica (arriba, delante, derecha) para que el corte se vea.
    const double sign = (plane == 1 ? 1.0 : -1.0) * (sectionFlip_->isChecked() ? -1.0 : 1.0);
    const double at = sectionOffset_->value();
    sketch::Vec3 origin{0.0, 0.0, 0.0};
    sketch::Vec3 normal{0.0, 0.0, sign};
    if (plane == 0) {
        origin.z = at;
    } else if (plane == 1) {
        origin.y = at;
        normal = {0.0, sign, 0.0};
    } else {
        origin.x = at;
        normal = {sign, 0.0, 0.0};
    }
    viewport_->setSectionPlane(origin, normal);
}

// Medir: mientras está activo, cada clic en una cara o arista se suma a la selección (una o dos) y
// el panel de la esquina de la vista dice qué es o qué relación tienen.
void MainWindow::toggleMeasure(bool on)
{
    if (measurePanel_ == nullptr) {
        measurePanel_ = new QLabel(viewport_);
        measurePanel_->setObjectName(QStringLiteral("measurePanel"));
        measurePanel_->setTextFormat(Qt::RichText);
        measurePanel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        measurePanel_->setStyleSheet(QStringLiteral(
            "QLabel#measurePanel { background: rgba(32, 36, 42, 225); color: #E8EBEF; border: 1px solid #4A515B; "
            "border-radius: 6px; padding: 8px 12px; }"));
        measurePanel_->move(16, 16);
        connect(viewport_, &render::Viewport::modelSelectionChanged, this, [this] {
            if (measureAction_ != nullptr && measureAction_->isChecked()) {
                updateMeasure();
            }
        });
    }
    viewport_->clearModelSelection();
    viewport_->setAdditiveSelection(on);
    if (on) {
        updateMeasure();
        measurePanel_->show();
        statusBar()->showMessage(QStringLiteral("Medir: elige una cara o arista y, para compararla, otra más · Esc termina"));
    } else {
        measurePanel_->hide();
        statusBar()->clearMessage();
    }
}

void MainWindow::updateMeasure()
{
    if (measurePanel_ == nullptr) {
        return;
    }
    std::vector<geometry::MeasureElement> elements;
    const render::ModelSelection selection = viewport_->modelSelection();
    const auto bodyOf = [this](const std::string& key) -> const document::Body* {
        for (const document::Body& body : document_.result().bodies) {
            if (body.key == key) {
                return &body;
            }
        }
        return nullptr;
    };
    for (const render::ModelSelection::Face& f : selection.faces) {
        if (const document::Body* body = bodyOf(f.body)) {
            if (const auto index = body->shape.findFace(f.ref)) {
                elements.push_back({body->shape, true, *index});
            }
        }
    }
    for (const render::ModelSelection::Edge& e : selection.edges) {
        if (const document::Body* body = bodyOf(e.body)) {
            if (const auto index = body->shape.findEdge(e.ref)) {
                elements.push_back({body->shape, false, *index});
            }
        }
    }
    const auto place = [this](const QString& html) {
        measurePanel_->setText(html);
        measurePanel_->adjustSize();
        measurePanel_->move(16, sectionPanel_ != nullptr && sectionPanel_->isVisible() ? sectionPanel_->geometry().bottom() + 10 : 16);
    };
    if (elements.empty()) {
        measureRun_.cancel();
        updateBusyBadge();
        place(QStringLiteral("<b>Medir</b><br>Elige una cara o una arista.<br>Con otra más, mide la distancia y el ángulo."));
        return;
    }
    // La distancia entre superficies complejas (una rosca) tarda: se mide en un hilo y el panel avisa mientras.
    // Si la selección cambia antes de acabar, el resultado viejo se descarta.
    place(QStringLiteral("<b>Medir</b> — midiendo…"));
    const bool moreThanTwo = selection.faces.size() + selection.edges.size() > 2;
    struct Outcome {
        geometry::Measurement measurement;
        QString error;
    };
    auto outcome = std::make_shared<Outcome>();
    measureRun_.start(
        [outcome, elements = std::move(elements)] {
            try {
                outcome->measurement = geometry::measure(elements);
            } catch (const std::exception& e) {
                outcome->error = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->error = QStringLiteral("error desconocido");
            }
        },
        [this, outcome, moreThanTwo, place] {
            updateBusyBadge();
            if (!outcome->error.isEmpty()) {
                place(QStringLiteral("<b>Medir</b> — no se pudo medir: ") + outcome->error.toHtmlEscaped());
                return;
            }
            QString html = QStringLiteral("<b>Medir</b> — ") + QString::fromStdString(outcome->measurement.title).toHtmlEscaped();
            for (const std::string& line : outcome->measurement.lines) {
                html += QStringLiteral("<br>") + QString::fromStdString(line).toHtmlEscaped();
            }
            if (moreThanTwo) {
                html += QStringLiteral("<br><i>Se miden las dos últimas por orden de lista.</i>");
            }
            place(html);
        });
    updateBusyBadge();
}

// Imprimir: revisa la pieza, la exporta a STL en una carpeta temporal y abre el slicer con ella.
// Mallar y escribir el STL va en un hilo aparte.
void MainWindow::printBodies()
{
    if (modelBusy()) {
        return;
    }
    if (exportRun_.running()) {
        statusBar()->showMessage(QStringLiteral("Ya se está exportando; espera a que termine"), 4000);
        return;
    }
    const std::vector<document::Body>& bodies = document_.result().bodies;
    std::vector<std::string> shown;
    for (const document::Body& body : bodies) {
        if (!hiddenBodies_.contains(body.key)) {
            shown.push_back(body.key);
        }
    }
    ExportDialog dialog(bodies, shown, this, ExportDialog::Mode::Print);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    std::vector<geometry::Shape> shapes;
    for (std::size_t index : dialog.selected()) {
        shapes.push_back(bodies[index].shape);
    }
    const QDir folder(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/NufioCAD"));
    folder.mkpath(QStringLiteral("."));
    // El nombre del diseño como nombre de archivo, sin caracteres que Windows no admite.
    QString base = QString::fromStdString(document_.name());
    for (const QChar bad : QStringLiteral("<>:\"/|?*")) {
        base.replace(bad, QLatin1Char('_'));
    }
    base.replace(QLatin1Char('\\'), QLatin1Char('_'));
    const QString path = folder.filePath(base + QStringLiteral(".stl"));
    const QString slicer = dialog.slicerPath();
    const geometry::MeshQuality quality = dialog.quality();

    struct Outcome {
        QString error;
    };
    auto outcome = std::make_shared<Outcome>();
    statusBar()->showMessage(QStringLiteral("Preparando el STL para el slicer… puedes seguir mirando el diseño"));
    exportRun_.start(
        [outcome, path, shapes = std::move(shapes), quality] {
            try {
                geometry::writeStl(std::filesystem::path(path.toStdU16String()), shapes, quality);
            } catch (const std::exception& e) {
                outcome->error = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->error = QStringLiteral("error desconocido");
            }
        },
        [this, outcome, path, slicer] {
            updateBusyBadge();
            statusBar()->clearMessage();
            if (!outcome->error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Imprimir"), outcome->error);
                return;
            }
            if (!QProcess::startDetached(slicer, {QDir::toNativeSeparators(path)})) {
                QMessageBox::warning(this, QStringLiteral("Imprimir"),
                                     QStringLiteral("No se pudo abrir el slicer:\n%1\n\nEl archivo quedó en:\n%2")
                                         .arg(QDir::toNativeSeparators(slicer), QDir::toNativeSeparators(path)));
                return;
            }
            statusBar()->showMessage(QStringLiteral("Abriendo %1 en el slicer").arg(QFileInfo(path).fileName()), 8000);
        });
    updateBusyBadge();
}

void MainWindow::exportBodies()
{
    if (modelBusy()) {
        return;
    }
    if (exportRun_.running()) {
        statusBar()->showMessage(QStringLiteral("Ya se está exportando; espera a que termine"), 4000);
        return;
    }
    const std::vector<document::Body>& bodies = document_.result().bodies;
    std::vector<std::string> shown;
    for (const document::Body& body : bodies) {
        if (!hiddenBodies_.contains(body.key)) {
            shown.push_back(body.key);
        }
    }
    ExportDialog dialog(bodies, shown, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString extension = ExportDialog::extension(dialog.format());
    const QString filter = QStringLiteral("%1 (*.%2)").arg(extension.toUpper(), extension);
    const std::vector<std::size_t> chosen = dialog.selected();
    const QString suggested = QString::fromStdString(document_.name());

    // Una lista de (archivo, cuerpos) según «un archivo por cuerpo».
    std::vector<ExportJob> jobs;
    if (dialog.onePerBody() && chosen.size() > 1) {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Carpeta donde exportar"));
        if (folder.isEmpty()) {
            return;
        }
        for (std::size_t index : chosen) {
            const QString name = QString::fromStdString(bodies[index].name);
            jobs.push_back({QDir(folder).filePath(name + QLatin1Char('.') + extension), {bodies[index].shape}, {bodies[index].name}});
        }
    } else {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar"), suggested + QLatin1Char('.') + extension,
                                                    filter);
        if (path.isEmpty()) {
            return;
        }
        if (QFileInfo(path).suffix().isEmpty()) {
            path += QLatin1Char('.') + extension;
        }
        std::vector<geometry::Shape> shapes;
        std::vector<std::string> names;
        for (std::size_t index : chosen) {
            shapes.push_back(bodies[index].shape);
            names.push_back(bodies[index].name);
        }
        jobs.push_back({path, std::move(shapes), std::move(names)});
    }

    // «Un archivo por cuerpo» no pregunta cada nombre: avisa antes de pisar.
    if (jobs.size() > 1) {
        QStringList existing;
        for (const ExportJob& job : jobs) {
            if (QFileInfo::exists(job.path)) {
                existing << QFileInfo(job.path).fileName();
            }
        }
        if (!existing.isEmpty()
            && QMessageBox::question(this, QStringLiteral("Exportar"),
                                     QStringLiteral("Ya existen y se reemplazarán:\n%1")
                                         .arg(existing.join(QLatin1Char('\n'))))
                   != QMessageBox::Yes) {
            return;
        }
    }
    startExport(std::move(jobs), dialog.format(), dialog.quality());
}

// Mallar y escribir los archivos va en un hilo de trabajo sobre las formas (inmutables y compartidas): con
// roscas o engranajes son segundos, y la ventana sigue viva mientras tanto.
void MainWindow::startExport(std::vector<ExportJob> jobs, ExportDialog::Format format, geometry::MeshQuality quality)
{
    if (exportRun_.running()) {
        statusBar()->showMessage(QStringLiteral("Ya se está exportando; espera a que termine"), 4000);
        return;
    }
    struct Outcome {
        QString error;
    };
    auto outcome = std::make_shared<Outcome>();
    const std::size_t count = jobs.size();
    const QString first = count == 0 ? QString() : QDir::toNativeSeparators(jobs.front().path);
    statusBar()->showMessage(count == 1 ? QStringLiteral("Exportando a %1… puedes seguir mirando el diseño").arg(first)
                                        : QStringLiteral("Exportando %1 archivos… puedes seguir mirando el diseño").arg(count));
    exportRun_.start(
        [outcome, jobs = std::move(jobs), format, quality] {
            try {
                for (const ExportJob& job : jobs) {
                    const std::filesystem::path out = job.path.toStdU16String();
                    switch (format) {
                    case ExportDialog::Format::Stl:
                        geometry::writeStl(out, job.shapes, quality);
                        break;
                    case ExportDialog::Format::Obj:
                        geometry::writeObj(out, job.shapes, quality);
                        break;
                    case ExportDialog::Format::Step:
                        geometry::writeStep(out, job.shapes);
                        break;
                    case ExportDialog::Format::ThreeMf: {
                        std::vector<geometry::ThreeMfPart> parts;
                        for (std::size_t i = 0; i < job.shapes.size(); ++i) {
                            parts.push_back({job.shapes[i], i < job.names.size() ? job.names[i] : "Cuerpo " + std::to_string(i + 1), 0});
                        }
                        geometry::writeThreeMf(out, parts, quality);
                        break;
                    }
                    }
                }
            } catch (const std::exception& e) {
                outcome->error = QString::fromUtf8(e.what());
            } catch (...) {
                outcome->error = QStringLiteral("error desconocido");
            }
        },
        [this, outcome, count, first] {
            updateBusyBadge();
            statusBar()->clearMessage();
            if (!outcome->error.isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Exportar"), outcome->error);
            } else {
                statusBar()->showMessage(count == 1 ? QStringLiteral("Exportado a %1").arg(first)
                                                    : QStringLiteral("Exportados %1 archivos").arg(count),
                                         8000);
            }
        });
    updateBusyBadge();
}

void MainWindow::refreshModel()
{
    if (!viewport_ || !timeline_ || !navigator_ || !modelStatusLabel_) {
        return;
    }
    const std::size_t upTo = shownUpTo();
    const document::ModelState& state = document_.stateBefore(upTo);
    updateExportAction();
    // Con el modelo calculándose en segundo plano, los estados de abajo son todavía los anteriores: rehacer los cuerpos,
    // los sketches y el navegador ahora sería trabajo perdido (al terminar el cálculo vuelve a avisar y se hace una vez).
    // Solo se refresca lo que ya es de verdad: el historial.
    if (document_.computing()) {
        timeline_->updateGeometry();
        timeline_->update();
        return;
    }

    // Con una herramienta activa, ella muestra su vista previa.
    if (!tool_) {
        std::vector<document::Body> visible;
        for (const document::Body& body : state.bodies) {
            if (!hiddenBodies_.contains(body.key)) {
                visible.push_back(body);
            }
        }
        viewport_->showBodies(visible);
        std::vector<render::Viewport::ConstructionPlaneView> planes;
        for (const auto& [id, plane] : state.planes) {
            if (hiddenPlanes_.contains(id)) {
                continue;
            }
            const document::Feature* f = document_.findFeature(id);
            planes.push_back({id, f ? QString::fromStdString(f->name()) : QString(), plane});
        }
        viewport_->showConstructionPlanes(planes);
        std::vector<render::Viewport::ConstructionAxisView> axes;
        for (const auto& [id, line] : state.axes) {
            if (hiddenPlanes_.contains(id)) {
                continue;
            }
            const document::Feature* f = document_.findFeature(id);
            axes.push_back({id, f ? QString::fromStdString(f->name()) : QString(), line.point, line.direction});
        }
        viewport_->showConstructionAxes(axes);
    }

    std::map<document::FeatureId, ShownSketch> wanted;
    std::set<document::FeatureId> visibleIds;
    for (std::size_t i = 0; i < upTo && i < document_.featureCount(); ++i) {
        const auto* sk = dynamic_cast<const document::SketchFeature*>(&document_.feature(i));
        if (sk == nullptr || sk->id() == editingSketch_) {
            continue;
        }
        const auto placed = state.sketches.find(sk->id());
        if (placed == state.sketches.end()) {
            continue;  // suprimido
        }
        // Al usar una herramienta, se ven todos (para elegir perfiles).
        if (!tool_ && !sketchVisible(sk->id(), upTo)) {
            continue;
        }
        visibleIds.insert(sk->id());
        // El calculado: lo proyectado del modelo ya está al día.
        wanted[sk->id()] = {placed->second.sketch.get(), placed->second.plane, sk->name()};
    }
    for (auto it = shownSketches_.begin(); it != shownSketches_.end();) {
        const auto keep = wanted.find(it->first);
        const auto samePoint = [](const sketch::Vec3& a, const sketch::Vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        };
        const auto samePlane = [&](const sketch::Plane& a, const sketch::Plane& b) {
            return samePoint(a.origin, b.origin) && samePoint(a.xAxis, b.xAxis) && samePoint(a.yAxis, b.yAxis);
        };
        const bool same = keep != wanted.end() && keep->second.sketch == it->second.sketch
                          && keep->second.name == it->second.name && samePlane(keep->second.plane, it->second.plane);
        if (!same) {
            viewport_->hideFinishedSketch(it->first);
            it = shownSketches_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [id, shown] : wanted) {
        if (!shownSketches_.contains(id)) {
            viewport_->showFinishedSketch(id, QString::fromStdString(shown.name), *shown.sketch, shown.plane);
            shownSketches_[id] = shown;
        }
    }
    navigator_->refresh(document_, state, upTo, hiddenBodies_, visibleIds, originVisible_, groundVisible_, hiddenPlanes_);

    // La primera operación con problemas, en la barra de estado.
    QString problem;
    int problems = 0;
    for (std::size_t i = 0; i < document_.featureCount(); ++i) {
        const document::Feature& f = document_.feature(i);
        const document::FeatureStatus& st = document_.status(f.id());
        if (st.health != document::Health::Error && st.health != document::Health::Warning) {
            continue;
        }
        if (problems++ == 0) {
            problem = QStringLiteral("%1 %2: %3")
                          .arg(st.health == document::Health::Error ? QStringLiteral("⛔") : QStringLiteral("⚠"),
                               QString::fromStdString(f.name()), QString::fromStdString(st.message));
        }
    }
    if (problems > 1) {
        problem += QStringLiteral(" (y %1 más)").arg(problems - 1);
    }
    modelStatusLabel_->setText(problem);
    modelStatusLabel_->setToolTip(problem);
    timeline_->updateGeometry();
    timeline_->update();
}

void MainWindow::clearNavigation()
{
    if (QAction* active = navGroup_ ? navGroup_->checkedAction() : nullptr) {
        active->setChecked(false);
    }
}

void MainWindow::cancel()
{
    if (recentsOpen_) {
        recentsOpen_ = false;
        updateStartScreen();
        return;
    }
    if (navGroup_ && navGroup_->checkedAction()) {
        clearNavigation();
        return;
    }
    if (measureAction_ != nullptr && measureAction_->isChecked()) {
        measureAction_->setChecked(false);
        toggleMeasure(false);
        return;
    }
    if (sectionAction_ != nullptr && sectionAction_->isChecked()) {
        sectionAction_->setChecked(false);
        toggleSection(false);
        return;
    }
    if (tool_) {
        finishTool(false);
        return;
    }
    if (sketch_ && sketch_->cancel()) {
        return;
    }
    if (viewport_->isPickingSketchPlane()) {
        viewport_->cancelPickSketchPlane();
        statusBar()->clearMessage();
        updateSketchActions();
    }
}

void MainWindow::updateSketchActions()
{
    updateExportAction();
    const bool picking = viewport_->isPickingSketchPlane();
    createSketchAction_->setEnabled(!sketch_ && !picking && !tool_);
    extrudeAction_->setEnabled(!sketch_ && !picking && !tool_);
    revolveAction_->setEnabled(!sketch_ && !picking && !tool_);
    sweepAction_->setEnabled(!sketch_ && !picking && !tool_);
    loftAction_->setEnabled(!sketch_ && !picking && !tool_);
    finishSketchAction_->setEnabled(sketch_ != nullptr);
    if (ribbon_ && (sketch_ != nullptr) != ribbonSketching_) {
        ribbonSketching_ = sketch_ != nullptr;
        ribbon_->setTabVisible(sketchTab_, ribbonSketching_);
        // Dentro de un sketch no hacen falta las herramientas generadoras: la pestaña se oculta (U2).
        ribbon_->setTabVisible(toolsTab_, !ribbonSketching_);
        ribbon_->setCurrentTab(ribbonSketching_ ? sketchTab_ : solidTab_);
    }
    updateSketchPalette();
    lookAtSketchAction_->setEnabled(sketch_ != nullptr);
    for (const auto& [tool, action] : toolActions_) {
        action->setEnabled(sketch_ != nullptr);
        if (!sketch_) {
            action->setChecked(false);
        }
    }
    deleteAction_->setEnabled(sketch_ && sketch_->hasSelection());
    constructionAction_->setEnabled(sketch_ && sketch_->hasSelection());
    lockAction_->setEnabled(sketch_ && sketch_->hasSelection());
    dimensionAction_->setEnabled(sketch_ != nullptr);
    intersectAction_->setEnabled(sketch_ != nullptr);
    importDxfAction_->setEnabled(sketch_ != nullptr);
    editTextAction_->setEnabled(sketch_ != nullptr);
    for (QAction* action : constraintActions_) {
        action->setEnabled(sketch_ != nullptr);
    }
}

} // namespace nuf::app
