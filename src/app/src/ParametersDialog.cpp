#include "ParametersDialog.hpp"
#include "DialogSize.hpp"
#include "Theme.hpp"

#include <nuf/document/Expression.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <set>

namespace nuf::app {

namespace {

enum Column { NameColumn, ExpressionColumn, ValueColumn };
constexpr int OriginalNameRole = Qt::UserRole;
constexpr int LastNameRole = Qt::UserRole + 1;

} // namespace

ParametersDialog::ParametersDialog(const std::vector<document::Parameter>& parameters, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Parámetros"));
    resize(560, 360);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral(
        "Dale un nombre a un valor y úsalo en las operaciones (por ejemplo en «Distancia»: <b>grosor * 2</b>).<br>"
        "Puedes usar + − * / ^, paréntesis, funciones (sin, cos, sqrt, min, max…; argumentos con «;»), "
        "unidades (mm, cm, m, in, deg) y otros parámetros.")));
    static_cast<QLabel*>(layout->itemAt(0)->widget())->setWordWrap(true);

    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({QStringLiteral("Nombre"), QStringLiteral("Expresión"), QStringLiteral("Valor")});
    table_->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(ExpressionColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(ValueColumn, QHeaderView::Interactive);
    table_->setColumnWidth(NameColumn, 140);
    table_->setColumnWidth(ValueColumn, 120);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table_, 1);

    problem_ = new QLabel();
    problem_->setWordWrap(true);
    layout->addWidget(problem_);

    auto* buttons = new QHBoxLayout();
    auto* add = new QPushButton(QStringLiteral("Añadir"));
    auto* remove = new QPushButton(QStringLiteral("Quitar"));
    ok_ = new QPushButton(QStringLiteral("Aceptar"));
    ok_->setDefault(true);
    auto* cancel = new QPushButton(QStringLiteral("Cancelar"));
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(ok_);
    layout->addLayout(buttons);

    for (const document::Parameter& p : parameters) {
        addRow(QString::fromStdString(p.name), QString::fromStdString(p.expression), true);
    }
    refresh();

    connect(add, &QPushButton::clicked, this, [this] {
        // Un nombre libre: p1, p2…
        std::set<QString> used;
        for (int row = 0; row < table_->rowCount(); ++row) {
            used.insert(table_->item(row, NameColumn)->text());
        }
        int n = 1;
        while (used.contains(QStringLiteral("p%1").arg(n))) {
            ++n;
        }
        addRow(QStringLiteral("p%1").arg(n), QStringLiteral("10"));
        refresh();
        table_->setCurrentCell(table_->rowCount() - 1, NameColumn);
        table_->editItem(table_->item(table_->rowCount() - 1, NameColumn));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int row = table_->currentRow();
        if (row >= 0) {
            table_->removeRow(row);
            refresh();
        }
    });
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!refreshing_ && item->column() == NameColumn) {
            followRename(item->row());
        }
        refresh();
    });
    connect(ok_, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    fitDialogToScreen(this, QStringLiteral("parameters"));
}

void ParametersDialog::addRow(const QString& name, const QString& expression, bool existing)
{
    refreshing_ = true;
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* nameItem = new QTableWidgetItem(name);
    // Nombre original (vacío si la fila es nueva) y último nombre ya propagado.
    nameItem->setData(OriginalNameRole, existing ? name : QString());
    nameItem->setData(LastNameRole, name);
    table_->setItem(row, NameColumn, nameItem);
    table_->setItem(row, ExpressionColumn, new QTableWidgetItem(expression));
    auto* value = new QTableWidgetItem();
    value->setFlags(value->flags() & ~Qt::ItemIsEditable);
    table_->setItem(row, ValueColumn, value);
    refreshing_ = false;
}

std::vector<document::Parameter> ParametersDialog::parameters() const
{
    std::vector<document::Parameter> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        result.push_back({table_->item(row, NameColumn)->text().trimmed().toStdString(),
                          table_->item(row, ExpressionColumn)->text().trimmed().toStdString()});
    }
    return result;
}

std::map<std::string, std::string> ParametersDialog::renames() const
{
    std::map<std::string, std::string> result;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const QTableWidgetItem* item = table_->item(row, NameColumn);
        const std::string original = item->data(OriginalNameRole).toString().toStdString();
        const std::string current = item->text().trimmed().toStdString();
        if (!original.empty() && original != current && document::isValidParameterName(current)) {
            result[original] = current;
        }
    }
    return result;
}

void ParametersDialog::followRename(int row)
{
    QTableWidgetItem* item = table_->item(row, NameColumn);
    const std::string last = item->data(LastNameRole).toString().toStdString();
    const std::string current = item->text().trimmed().toStdString();
    if (last == current || !document::isValidParameterName(current) || !document::isValidParameterName(last)) {
        return;
    }
    for (int other = 0; other < table_->rowCount(); ++other) {
        if (other != row && table_->item(other, NameColumn)->text().trimmed().toStdString() == current) {
            return;  // nombre repetido: se avisa en refresh(), y se propaga cuando se corrija
        }
    }
    refreshing_ = true;
    for (int other = 0; other < table_->rowCount(); ++other) {
        if (other == row) {
            continue;
        }
        QTableWidgetItem* expression = table_->item(other, ExpressionColumn);
        const QString updated = QString::fromStdString(
            document::renameInExpression(expression->text().toStdString(), {{last, current}}));
        if (updated != expression->text()) {
            expression->setText(updated);
        }
    }
    item->setData(LastNameRole, QString::fromStdString(current));
    refreshing_ = false;
}

void ParametersDialog::refresh()
{
    if (refreshing_) {
        return;
    }
    refreshing_ = true;
    const std::vector<document::Parameter> list = parameters();
    // Un documento vacío solo para evaluar la tabla.
    document::Document scratch;
    scratch.setParameters(list);

    QString problem;
    std::set<std::string> seen;
    for (int row = 0; row < int(list.size()); ++row) {
        const document::Parameter& p = list[std::size_t(row)];
        QString shown;
        QString error;
        if (!document::isValidParameterName(p.name)) {
            error = QStringLiteral("«%1» no es un nombre válido (letras, números y «_»; no puede empezar por número ni ser una función)")
                        .arg(QString::fromStdString(p.name));
        } else if (!seen.insert(p.name).second) {
            error = QStringLiteral("El nombre «%1» está repetido").arg(QString::fromStdString(p.name));
        } else if (const auto value = scratch.parameterValue(p.name); value.value) {
            shown = QLocale().toString(*value.value, 'g', 10);
        } else {
            error = QStringLiteral("%1: %2").arg(QString::fromStdString(p.name), QString::fromStdString(value.error));
        }
        QTableWidgetItem* item = table_->item(row, ValueColumn);
        item->setText(error.isEmpty() ? shown : QStringLiteral("⚠"));
        item->setToolTip(error);
        if (!error.isEmpty() && problem.isEmpty()) {
            problem = error;
        }
    }
    problem_->setText(problem.isEmpty() ? QString() : QStringLiteral("<span style='color:%1'>%2</span>").arg(toneColor(Tone::Error), problem.toHtmlEscaped()));
    ok_->setEnabled(problem.isEmpty());
    refreshing_ = false;
}

} // namespace nuf::app
