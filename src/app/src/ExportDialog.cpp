#include "ExportDialog.hpp"
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

    quality_ = new QComboBox(this);
    quality_->addItem(QStringLiteral("Baja (rápida, facetada)"));
    quality_->addItem(QStringLiteral("Media"));
    quality_->addItem(QStringLiteral("Alta (curvas suaves, archivo grande)"));
    quality_->setCurrentIndex(1);

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
    form->addRow(QStringLiteral("Cuerpos:"), list_);
    form->addRow(QString(), onePerBody_);

    // La impresora del usuario: con sus medidas se calcula la revisión.
    QSettings settings;
    const auto spin = [&](const char* key, double value, double low, double high, const QString& suffix) {
        auto* box = new QDoubleSpinBox(this);
        box->setRange(low, high);
        box->setDecimals(2);
        setUnit(box, suffix.trimmed());
        box->setValue(settings.value(QLatin1String(key), value).toDouble());
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
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Elegir el slicer"), QString(),
                                                          QStringLiteral("Programas (*.exe);;Todos los archivos (*)"));
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
    connect(list_, &QListWidget::itemChanged, this, &ExportDialog::updateSummary);
    updateSummary();
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
    }
    return {};
}

std::vector<geometry::Shape> ExportDialog::selectedShapes() const
{
    std::vector<geometry::Shape> shapes;
    for (std::size_t index : selected()) {
        shapes.push_back(bodies_[index].shape);
    }
    return shapes;
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
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const std::vector<geometry::PrintIssue> issues = geometry::checkPrintable(chosen, printSettings());
    QApplication::restoreOverrideCursor();
    if (issues.empty()) {
        review_->setText(QStringLiteral("<span style='color:#3FB950'>✓ Todo en orden para imprimir con tu impresora.</span>"));
        return;
    }
    QStringList lines;
    for (const geometry::PrintIssue& issue : issues) {
        const char* color = issue.level == geometry::PrintIssue::Level::Error ? "#E5484D"
                            : issue.level == geometry::PrintIssue::Level::Warning ? "#F2A34B"
                                                                                   : "#8B949E";
        const QString icon = issue.level == geometry::PrintIssue::Level::Error ? QStringLiteral("⛔")
                             : issue.level == geometry::PrintIssue::Level::Warning ? QStringLiteral("⚠")
                                                                                   : QStringLiteral("ℹ");
        lines << QStringLiteral("<span style='color:%1'>%2 %3</span>")
                     .arg(QLatin1String(color), icon, QString::fromStdString(issue.text).toHtmlEscaped());
    }
    review_->setText(lines.join(QStringLiteral("<br>")));
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
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const std::size_t triangles = geometry::countTriangles(selectedShapes(), quality());
        QApplication::restoreOverrideCursor();
        summary_->setText(QStringLiteral("Unos %1 triángulos, en milímetros.").arg(QLocale().toString(qlonglong(triangles))));
    }
}

} // namespace nuf::app
