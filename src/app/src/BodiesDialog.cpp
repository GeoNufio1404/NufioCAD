#include "BodiesDialog.hpp"
#include "DialogSize.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nuf::app {

namespace {
enum Column { KeyColumn, NameColumn, ComponentColumn };
}

BodiesDialog::BodiesDialog(const document::Document& document, QWidget* parent)
    : QDialog(parent)
    , original_(document.bodyInfo())
{
    setWindowTitle(QStringLiteral("Cuerpos y componentes"));
    resize(560, 340);
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(QStringLiteral(
        "Ponle un nombre a cada cuerpo y agrúpalos en <b>componentes</b> escribiendo el mismo nombre de componente "
        "(por ejemplo «Carcasa»). En el navegador cada componente es una carpeta que se muestra u oculta de una vez."));
    help->setWordWrap(true);
    layout->addWidget(help);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({QStringLiteral("Cuerpo"), QStringLiteral("Nombre"), QStringLiteral("Componente")});
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ComponentColumn, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    // Los cuerpos que hay ahora; su nombre de fábrica se ve como pista.
    for (const document::Body& body : document.result().bodies) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        keys_.push_back(body.key);
        const auto info = original_.find(body.key);
        auto* key = new QTableWidgetItem(QString::fromStdString(body.key));
        key->setFlags(key->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, KeyColumn, key);
        table_->setItem(row, NameColumn,
                        new QTableWidgetItem(QString::fromStdString(info != original_.end() && !info->second.name.empty()
                                                                         ? info->second.name
                                                                         : body.name)));
        table_->setItem(row, ComponentColumn,
                        new QTableWidgetItem(QString::fromStdString(info != original_.end() ? info->second.component : std::string())));
    }

    auto* buttons = new QHBoxLayout();
    auto* ok = new QPushButton(QStringLiteral("Aceptar"));
    ok->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    fitDialogToScreen(this, QStringLiteral("bodies"));
}

std::map<std::string, document::BodyInfo> BodiesDialog::info() const
{
    std::map<std::string, document::BodyInfo> result = original_;  // lo de cuerpos que ahora no se ven
    for (int row = 0; row < table_->rowCount(); ++row) {
        document::BodyInfo info;
        info.name = table_->item(row, NameColumn)->text().trimmed().toStdString();
        info.component = table_->item(row, ComponentColumn)->text().trimmed().toStdString();
        result[keys_[std::size_t(row)]] = info;
    }
    return result;
}

} // namespace nuf::app
