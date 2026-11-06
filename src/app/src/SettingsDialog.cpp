#include "SettingsDialog.hpp"

#include "Units.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace nuf::app {

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Configuración"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    unit_ = new QComboBox(this);
    for (const LengthUnit unit : {LengthUnit::Mm, LengthUnit::Cm, LengthUnit::M, LengthUnit::In}) {
        unit_->addItem(Units::name(unit), int(unit));
    }
    unit_->setCurrentIndex(unit_->findData(int(Units::instance().length())));
    form->addRow(QStringLiteral("Unidad de longitud"), unit_);
    layout->addLayout(form);
    auto* note = new QLabel(QStringLiteral("Cambia cómo se muestran y se escriben las medidas. El diseño y los "
                                           "archivos siguen guardándose en milímetros: no se pierde precisión."),
                            this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    // Se aplica en cuanto se elige.
    connect(unit_, &QComboBox::currentIndexChanged, this, [this] {
        Units::instance().setLength(LengthUnit(unit_->currentData().toInt()));
    });
}

} // namespace nuf::app
