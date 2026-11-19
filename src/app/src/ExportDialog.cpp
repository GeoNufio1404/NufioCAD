#include "ExportDialog.hpp"
#include "DialogSize.hpp"
#include "Theme.hpp"
#include "ExpressionSpinBox.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLineEdit>
#include <QGroupBox>
#include <QSettings>
#include <QTimer>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdio>
#include <memory>

namespace nuf::app {

ExportDialog::ExportDialog(const std::vector<document::Body>& bodies, const std::vector<std::string>& checkedKeys,
                           QWidget* parent, Mode mode)
    : QDialog(parent)
    , bodies_(bodies)
    , mode_(mode)
{
    setWindowTitle(mode == Mode::Print ? QStringLiteral("Imprimir") : QStringLiteral("Exportar"));
    setMinimumWidth(380);

    format_ = new QComboBox(this);
    format_->addItem(QStringLiteral("STL — malla para imprimir (.stl)"), int(Format::Stl));
    format_->addItem(QStringLiteral("OBJ — malla (.obj)"), int(Format::Obj));
    format_->addItem(QStringLiteral("STEP — sólido exacto (.step)"), int(Format::Step));
    format_->addItem(QStringLiteral("3MF — varios cuerpos con color por filamento (.3mf)"), int(Format::ThreeMf));

    quality_ = new QComboBox(this);
    quality_->addItem(QStringLiteral("Baja (rápida, facetada)"));
    quality_->addItem(QStringLiteral("Media"));
    quality_->addItem(QStringLiteral("Alta (curvas suaves, archivo grande)"));
    quality_->addItem(QStringLiteral("Personalizada…"));
    quality_->setCurrentIndex(1);
    // Personalizada (O6): lo lejos que puede quedar la malla de la superficie real y el giro máximo entre triángulos vecinos.
    customLinear_ = new QDoubleSpinBox(this);
    customLinear_->setRange(0.001, 5.0);
    customLinear_->setDecimals(3);
    customLinear_->setSingleStep(0.01);
    customLinear_->setSuffix(QStringLiteral(" mm"));
    customLinear_->setValue(QSettings().value(QStringLiteral("export/customLinear"), 0.05).toDouble());
    customLinear_->setToolTip(QStringLiteral("Cuánto puede apartarse la malla de la superficie real: menos, más fiel y más triángulos"));
    customAngular_ = new QDoubleSpinBox(this);
    customAngular_->setRange(1.0, 60.0);
    customAngular_->setDecimals(1);
    customAngular_->setSuffix(QStringLiteral("°"));
    customAngular_->setValue(QSettings().value(QStringLiteral("export/customAngular"), 15.0).toDouble());
    customAngular_->setToolTip(QStringLiteral("El giro máximo entre dos triángulos vecinos en una curva"));

    list_ = new QListWidget(this);
    list_->setMaximumHeight(130);
    for (const document::Body& body : bodies_) {
        auto* item = new QListWidgetItem(QString::fromStdString(body.name), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool checked = std::ranges::find(checkedKeys, body.key) != checkedKeys.end();
        item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }

    onePerBody_ = new QCheckBox(QStringLiteral("Un archivo por cuerpo"), this);
    summary_ = new QLabel(this);
    summary_->setForegroundRole(QPalette::PlaceholderText);
    summary_->setWordWrap(true);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Formato:"), format_);
    form->addRow(QStringLiteral("Calidad de malla:"), quality_);
    form->addRow(QStringLiteral("  Desviación máxima:"), customLinear_);
    form->addRow(QStringLiteral("  Giro máximo:"), customAngular_);
    const auto showCustom = [this, form] {
        const bool custom = quality_->currentIndex() == 3;
        form->setRowVisible(customLinear_, custom);
        form->setRowVisible(customAngular_, custom);
    };
    showCustom();
    connect(quality_, &QComboBox::currentIndexChanged, this, showCustom);
    form->addRow(QStringLiteral("Cuerpos:"), list_);
    form->addRow(QString(), onePerBody_);

    // La impresora del usuario: con sus medidas se calcula la revisión.
    QSettings settings;
    const auto spin = [&](const char* key, double value, double low, double high, const QString& suffix) {
        auto* box = new ExpressionSpinBox(this);
        box->setRange(low, high);
        box->setDecimals(2);
        Q_UNUSED(suffix);
        box->setValue(settings.value(QLatin1String(key), value).toDouble());
        box->setLength(true);
        box->setKeyboardTracking(false);
        return box;
    };
    nozzle_ = spin("print/nozzle", 0.4, 0.1, 2.0, QStringLiteral(" mm"));
    bedX_ = spin("print/bedX", 256.0, 10.0, 2000.0, QStringLiteral(" mm"));
    bedY_ = spin("print/bedY", 256.0, 10.0, 2000.0, QStringLiteral(" mm"));
    bedZ_ = spin("print/bedZ", 256.0, 10.0, 2000.0, QStringLiteral(" mm"));
    auto* printerBox = new QGroupBox(QStringLiteral("Mi impresora"), this);
    auto* printerForm = new QFormLayout(printerBox);
    printerForm->addRow(QStringLiteral("Boquilla:"), nozzle_);
    auto* bed = new QHBoxLayout();
    bed->addWidget(bedX_);
    bed->addWidget(bedY_);
    bed->addWidget(bedZ_);
    printerForm->addRow(QStringLiteral("Cama X × Y × Z:"), bed);
    slicer_ = new QLineEdit(settings.value(QStringLiteral("print/slicer")).toString(), this);
    slicer_->setPlaceholderText(QStringLiteral("Programa de laminado (Bambu Studio, Orca…)"));
    auto* choose = new QPushButton(QStringLiteral("Elegir…"), this);
    auto* slicerRow = new QHBoxLayout();
    slicerRow->addWidget(slicer_, 1);
    slicerRow->addWidget(choose);
    if (mode == Mode::Print) {
        printerForm->addRow(QStringLiteral("Slicer:"), slicerRow);
    } else {
        slicer_->hide();
        choose->hide();
    }
    connect(choose, &QPushButton::clicked, this, [this] {
#ifdef Q_OS_WIN
        // En Windows los programas son *.exe; en Linux y macOS no tienen extensión, así que no se filtra.
        const QString filter = QStringLiteral("Programas (*.exe);;Todos los archivos (*)");
#else
        const QString filter = QStringLiteral("Todos los archivos (*)");
#endif
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Elegir el slicer"), QString(), filter);
        if (!path.isEmpty()) {
            slicer_->setText(path);
        }
    });
    connect(slicer_, &QLineEdit::textChanged, this, [this] {
        QSettings().setValue(QStringLiteral("print/slicer"), slicer_->text());
        updateSummary();
    });
    printer_ = printerBox;

    review_ = new QLabel(this);
    review_->setWordWrap(true);
    review_->setTextFormat(Qt::RichText);
    reviewTimer_ = new QTimer(this);
    reviewTimer_->setSingleShot(true);
    reviewTimer_->setInterval(250);
    connect(reviewTimer_, &QTimer::timeout, this, &ExportDialog::runReview);
    for (QDoubleSpinBox* box : {nozzle_, bedX_, bedY_, bedZ_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            savePrinterSettings();
            reviewTimer_->start();
        });
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(mode == Mode::Print ? QStringLiteral("Imprimir") : QStringLiteral("Exportar…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(summary_);
    layout->addWidget(review_);
    layout->addWidget(printer_);
    layout->addWidget(buttons);

    if (mode == Mode::Print) {
        format_->setCurrentIndex(0);  // siempre STL
        format_->setEnabled(false);
        onePerBody_->hide();
    }
    connect(format_, &QComboBox::currentIndexChanged, this, &ExportDialog::updateSummary);
    connect(quality_, &QComboBox::currentIndexChanged, this, &ExportDialog::updateSummary);
    for (QDoubleSpinBox* box : {customLinear_, customAngular_}) {
        connect(box, &QDoubleSpinBox::valueChanged, this, [this] {
            if (quality_->currentIndex() == 3) {
                QSettings settings;
                settings.setValue(QStringLiteral("export/customLinear"), customLinear_->value());
                settings.setValue(QStringLiteral("export/customAngular"), customAngular_->value());
                triangles_.clear();  // los conteos de antes eran con otra malla
                updateSummary();
            }
        });
    }
    connect(list_, &QListWidget::itemChanged, this, &ExportDialog::updateSummary);
    updateSummary();
    fitDialogToScreen(this, QStringLiteral("export"));
}

ExportDialog::Format ExportDialog::format() const
{
    return static_cast<Format>(format_->currentData().toInt());
}

geometry::MeshQuality ExportDialog::quality() const
{
    switch (quality_->currentIndex()) {
    case 0:
        return geometry::MeshQuality::low();
    case 2:
        return geometry::MeshQuality::high();
    case 3:
        return {customLinear_->value(), customAngular_->value() * 3.14159265358979323846 / 180.0};
    default:
        return geometry::MeshQuality::medium();
    }
}

bool ExportDialog::onePerBody() const
{
    return mode_ == Mode::Export && onePerBody_->isEnabled() && onePerBody_->isChecked();
}

QString ExportDialog::slicerPath() const
{
    return slicer_->text().trimmed();
}

std::vector<std::size_t> ExportDialog::selected() const
{
    std::vector<std::size_t> result;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) {
            result.push_back(static_cast<std::size_t>(i));
        }
    }
    return result;
}

QString ExportDialog::extension(Format format)
{
    switch (format) {
    case Format::Stl:
        return QStringLiteral("stl");
    case Format::Obj:
        return QStringLiteral("obj");
    case Format::Step:
        return QStringLiteral("step");
    case Format::ThreeMf:
        return QStringLiteral("3mf");
    }
    return {};
}

geometry::PrintSettings ExportDialog::printSettings() const
{
    geometry::PrintSettings settings;
    settings.nozzle = nozzle_->value();
    settings.bedX = bedX_->value();
    settings.bedY = bedY_->value();
    settings.bedZ = bedZ_->value();
    return settings;
}

void ExportDialog::savePrinterSettings() const
{
    QSettings settings;
    settings.setValue(QStringLiteral("print/nozzle"), nozzle_->value());
    settings.setValue(QStringLiteral("print/bedX"), bedX_->value());
    settings.setValue(QStringLiteral("print/bedY"), bedY_->value());
    settings.setValue(QStringLiteral("print/bedZ"), bedZ_->value());
}

void ExportDialog::runReview()
{
    std::vector<std::pair<std::string, geometry::Shape>> chosen;
    for (std::size_t index : selected()) {
        chosen.emplace_back(bodies_[index].name, bodies_[index].shape);
    }
    if (chosen.empty() || format() == Format::Step) {
        return;
    }
    // La revisión (booleanas entre cuerpos, validez, paredes) corre en un hilo sobre copias de las formas.
    struct Review {
        std::vector<geometry::PrintIssue> issues;
        std::string failure;
    };
    auto review = std::make_shared<Review>();
    const geometry::PrintSettings settings = printSettings();
    const geometry::MeshQuality meshQuality = quality();
    reviewRun_.start(
        [review, chosen = std::move(chosen), settings, meshQuality] {
            try {
                review->issues = geometry::checkPrintable(chosen, settings);
                // La malla que saldría con la calidad elegida: cerrada, bien orientada y fiel al sólido.
                for (const auto& [name, shape] : chosen) {
                    const geometry::MeshReport mesh = geometry::analyzeMesh(shape, meshQuality);
                    using Level = geometry::PrintIssue::Level;
                    const std::string label = "«" + name + "»: ";
                    if (mesh.openEdges > 0) {
                        review->issues.push_back({Level::Error, label + "la malla queda abierta (" + std::to_string(mesh.openEdges) +
                                                                    " bordes sin pareja): el slicer puede decir que no es estanca"});
                    }
                    if (mesh.nonManifoldEdges > 0) {
                        review->issues.push_back({Level::Warning, label + "la malla tiene " + std::to_string(mesh.nonManifoldEdges) +
                                                                      " bordes compartidos por más de dos triángulos"});
                    }
                    if (mesh.flippedEdges > 0) {
                        review->issues.push_back({Level::Warning, label + "hay triángulos orientados al revés (" +
                                                                      std::to_string(mesh.flippedEdges) + " bordes)"});
                    }
                    if (mesh.degenerateTriangles > 0) {
                        review->issues.push_back({Level::Info, label + std::to_string(mesh.degenerateTriangles) +
                                                                   " triángulos casi sin área (el slicer los ignora)"});
                    }
                    if (mesh.volumeDeviation() > 0.02) {
                        char percent[16];
                        std::snprintf(percent, sizeof percent, "%.1f", 100.0 * mesh.volumeDeviation());
                        review->issues.push_back({Level::Warning, label + "con esta calidad la malla se aparta un " + percent +
                                                                      " % del volumen del sólido: sube la calidad para curvas más fieles"});
                    }
                }
            } catch (const std::exception& e) {
                review->failure = e.what();
            } catch (...) {
                review->failure = "error desconocido";
            }
        },
        [this, review] { showReview(review->issues, review->failure); });
}

void ExportDialog::showReview(const std::vector<geometry::PrintIssue>& issues, const std::string& failure)
{
    if (!failure.empty()) {
        review_->setText(QStringLiteral("<span style='color:%1'>No se pudo revisar la pieza: %2</span>")
                             .arg(toneColor(Tone::Warning), QString::fromStdString(failure).toHtmlEscaped()));
        return;
    }
    if (issues.empty()) {
        review_->setText(QStringLiteral("<span style='color:%1'>✓ Todo en orden para imprimir con tu impresora.</span>")
                             .arg(toneColor(Tone::Success)));
        return;
    }
    QStringList lines;
    for (const geometry::PrintIssue& issue : issues) {
        const QString color = toneColor(issue.level == geometry::PrintIssue::Level::Error     ? Tone::Error
                                        : issue.level == geometry::PrintIssue::Level::Warning ? Tone::Warning
                                                                                               : Tone::Muted);
        const QString icon = issue.level == geometry::PrintIssue::Level::Error ? QStringLiteral("⛔")
                             : issue.level == geometry::PrintIssue::Level::Warning ? QStringLiteral("⚠")
                                                                                   : QStringLiteral("ℹ");
        lines << QStringLiteral("<span style='color:%1'>%2 %3</span>")
                     .arg(color, icon, QString::fromStdString(issue.text).toHtmlEscaped());
    }
    review_->setText(lines.join(QStringLiteral("<br>")));
}

void ExportDialog::refreshTriangleCount()
{
    if (countRun_.running()) {
        countAgain_ = true;  // al terminar se vuelve a mirar con la selección de entonces
        return;
    }
    const int quality = quality_->currentIndex();
    std::vector<std::pair<std::pair<const void*, int>, geometry::Shape>> missing;
    for (std::size_t index : selected()) {
        const geometry::Shape& shape = bodies_[index].shape;
        const std::pair<const void*, int> key{shape.impl(), quality};
        if (!triangles_.contains(key) && std::ranges::none_of(missing, [&](const auto& m) { return m.first == key; })) {
            missing.emplace_back(key, shape);
        }
    }
    if (missing.empty()) {
        showTriangleCount();
        return;
    }
    summary_->setText(QStringLiteral("Contando los triángulos…"));
    struct Count {
        std::vector<std::pair<std::pair<const void*, int>, geometry::Shape>> bodies;
        std::vector<std::size_t> counts;
    };
    auto count = std::make_shared<Count>();
    count->bodies = std::move(missing);
    const geometry::MeshQuality meshQuality = this->quality();
    countRun_.start(
        [count, meshQuality] {
            try {
                for (const auto& body : count->bodies) {
                    count->counts.push_back(geometry::countTriangles({body.second}, meshQuality));
                }
            } catch (...) {
                // Lo que no se pudo contar se queda sin número (ver abajo).
            }
        },
        [this, count] {
            for (std::size_t i = 0; i < count->counts.size(); ++i) {
                triangles_[count->bodies[i].first] = count->counts[i];
            }
            if (countAgain_ || count->counts.size() < count->bodies.size()) {
                countAgain_ = false;
                if (count->counts.size() < count->bodies.size()) {
                    summary_->setText(QStringLiteral("No se pudieron contar los triángulos."));
                    return;
                }
                refreshTriangleCount();
                return;
            }
            showTriangleCount();
        });
}

void ExportDialog::showTriangleCount()
{
    const int quality = quality_->currentIndex();
    std::size_t total = 0;
    for (std::size_t index : selected()) {
        const auto found = triangles_.find({bodies_[index].shape.impl(), quality});
        if (found != triangles_.end()) {
            total += found->second;
        }
    }
    summary_->setText(QStringLiteral("Unos %1 triángulos, en milímetros.").arg(QLocale().toString(qlonglong(total))));
}

void ExportDialog::updateSummary()
{
    const std::size_t count = selected().size();
    const bool mesh = format() != Format::Step;
    quality_->setEnabled(mesh);
    review_->setVisible(mesh && count > 0);
    printer_->setVisible(mesh);
    if (mesh && count > 0) {
        review_->setText(QStringLiteral("Revisando la pieza para imprimir…"));
        reviewRun_.cancel();  // lo que estuviera revisándose ya no vale
        reviewTimer_->start();
    }
    onePerBody_->setEnabled(count > 1);
    ok_->setEnabled(count > 0 && (mode_ != Mode::Print || QFileInfo(slicerPath()).isFile()));

    if (count == 0) {
        summary_->setText(QStringLiteral("Marca al menos un cuerpo."));
    } else if (mode_ == Mode::Print && !QFileInfo(slicerPath()).isFile()) {
        summary_->setText(QStringLiteral("Elige el programa de tu slicer (abajo) para poder imprimir."));
    } else if (!mesh) {
        summary_->setText(QStringLiteral("Sólido exacto en milímetros, sin mallar."));
    } else {
        refreshTriangleCount();
    }
}

} // namespace nuf::app
