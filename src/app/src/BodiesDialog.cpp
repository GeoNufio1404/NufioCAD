#include "BodiesDialog.hpp"
#include "DialogSize.hpp"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace nuf::app {

namespace {
enum Column { KeyColumn, NameColumn, ComponentColumn, ColorColumn };
}

BodiesDialog::BodiesDialog(const document::Document& document, QWidget* parent)
    : QDialog(parent)
    , original_(document.bodyInfo())
{
    setWindowTitle(QStringLiteral("Cuerpos y componentes"));
    resize(640, 340);
    auto* layout = new QVBoxLayout(this);
    auto* help = new QLabel(QStringLiteral(
        "Ponle un nombre a cada cuerpo y agrúpalos en <b>componentes</b> escribiendo el mismo nombre de componente "
        "(por ejemplo «Carcasa»). En el navegador cada componente es una carpeta que se muestra u oculta de una vez. Con doble clic en la columna <b>Color</b> eliges el color con el que se ve el cuerpo (clic derecho: quitarlo)."));
    help->setWordWrap(true);
    layout->addWidget(help);

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({QStringLiteral("Cuerpo"), QStringLiteral("Nombre"), QStringLiteral("Componente"), QStringLiteral("Color")});
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ComponentColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ColorColumn, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
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
        colors_.push_back(info != original_.end() ? info->second.color : std::string());
        auto* colorItem = new QTableWidgetItem();
        colorItem->setFlags(colorItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(row, ColorColumn, colorItem);
        paintColorCell(row);
    }

    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column != ColorColumn) {
            return;
        }
        const QString current = colors_[std::size_t(row)].empty() ? QStringLiteral("#B7BEC7") : QString::fromStdString(colors_[std::size_t(row)]);
        const QColor chosen = QColorDialog::getColor(QColor(current), this, QStringLiteral("Color del cuerpo"));
        if (chosen.isValid()) {
            colors_[std::size_t(row)] = chosen.name(QColor::HexRgb).toUpper().toStdString();
            paintColorCell(row);
        }
    });
    connect(table_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = table_->rowAt(pos.y());
        if (row >= 0 && table_->columnAt(pos.x()) == ColorColumn) {
            colors_[std::size_t(row)].clear();
            paintColorCell(row);
        }
    });

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

void BodiesDialog::paintColorCell(int row)
{
    QTableWidgetItem* item = table_->item(row, ColorColumn);
    const std::string& color = colors_[std::size_t(row)];
    if (color.empty()) {
        item->setBackground(QBrush());
        item->setText(QStringLiteral("— de siempre —"));
        item->setToolTip(QStringLiteral("Doble clic para elegir un color"));
    } else {
        const QColor shade(QString::fromStdString(color));
        item->setBackground(shade);
        item->setForeground(shade.lightness() > 140 ? Qt::black : Qt::white);
        item->setText(QString::fromStdString(color));
        item->setToolTip(QStringLiteral("Doble clic para cambiarlo; clic derecho para quitarlo"));
    }
}

std::map<std::string, document::BodyInfo> BodiesDialog::info() const
{
    std::map<std::string, document::BodyInfo> result = original_;  // lo de cuerpos que ahora no se ven
    for (int row = 0; row < table_->rowCount(); ++row) {
        document::BodyInfo info;
        info.name = table_->item(row, NameColumn)->text().trimmed().toStdString();
        info.component = table_->item(row, ComponentColumn)->text().trimmed().toStdString();
        info.color = colors_[std::size_t(row)];
        // Lo fijo no se cambia aquí (se hace con la chincheta del navegador): se conserva.
        if (const auto old = original_.find(keys_[std::size_t(row)]); old != original_.end()) {
            info.fixed = old->second.fixed;
        }
        result[keys_[std::size_t(row)]] = info;
    }
    return result;
}

} // namespace nuf::app
