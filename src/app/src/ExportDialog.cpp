#include "ExportDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace nuf::app {

ExportDialog::ExportDialog(const std::vector<document::Body>& bodies, const std::vector<std::string>& checkedKeys,
                           QWidget* parent)
    : QDialog(parent)
    , bodies_(bodies)
{
    setWindowTitle(QStringLiteral("Exportar"));
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

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(QStringLiteral("Exportar…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(summary_);
    layout->addWidget(buttons);

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
    return onePerBody_->isEnabled() && onePerBody_->isChecked();
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

void ExportDialog::updateSummary()
{
    const std::size_t count = selected().size();
    const bool mesh = format() != Format::Step;
    quality_->setEnabled(mesh);
    onePerBody_->setEnabled(count > 1);
    ok_->setEnabled(count > 0);

    if (count == 0) {
        summary_->setText(QStringLiteral("Marca al menos un cuerpo."));
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
