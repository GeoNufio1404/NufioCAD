#include "InterferenceDialog.hpp"

#include "DialogSize.hpp"
#include "Units.hpp"

#include <nuf/document/Features.hpp>
#include <nuf/geometry/DisplayMesh.hpp>
#include <nuf/render/Viewport.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <numbers>

namespace nuf::app {

namespace {
enum Column { FirstColumn, SecondColumn, VolumeColumn, AtColumn };
}

InterferenceDialog::InterferenceDialog(const document::Document& document, render::Viewport& viewport,
                                       const std::set<std::string>& hiddenBodies, QWidget* parent)
    : QDialog(parent)
    , document_(document)
    , viewport_(viewport)
    , hidden_(hiddenBodies)
{
    setWindowTitle(QStringLiteral("Interferencias"));
    resize(620, 440);
    auto* layout = new QVBoxLayout(this);
    summary_ = new QLabel();
    summary_->setWordWrap(true);
    layout->addWidget(summary_);

    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({QStringLiteral("Cuerpo"), QStringLiteral("Con"), QStringLiteral("Volumen común (mm³)"),
                                       QStringLiteral("Peor a (°)")});
    table_->horizontalHeader()->setSectionResizeMode(FirstColumn, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(SecondColumn, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    // A lo largo del giro de un enlace de movimiento.
    auto* along = new QHBoxLayout();
    links_ = new QComboBox();
    for (std::size_t i = 0; i < document.featureCount(); ++i) {
        const document::Feature& f = document.feature(i);
        if (f.kind() == document::FeatureKind::MotionLink && !f.suppressed()) {
            links_->addItem(QString::fromStdString(f.name()), QVariant::fromValue<qulonglong>(f.id()));
        }
    }
    span_ = new QDoubleSpinBox();
    span_->setRange(1.0, 3600.0);
    span_->setDecimals(0);
    span_->setValue(360.0);
    span_->setSuffix(QStringLiteral("°"));
    span_->setToolTip(QStringLiteral("Cuánto gira el primer miembro del enlace durante la revisión"));
    steps_ = new QSpinBox();
    steps_->setRange(2, 720);
    steps_->setValue(36);
    steps_->setSuffix(QStringLiteral(" posiciones"));
    checkAlong_ = new QPushButton(QStringLiteral("Revisar el giro"));
    checkAlong_->setToolTip(QStringLiteral("Mira las posiciones del giro una a una y da, de cada pareja que se pisa, el peor momento"));
    along->addWidget(new QLabel(QStringLiteral("A lo largo del giro de")));
    along->addWidget(links_, 1);
    along->addWidget(span_);
    along->addWidget(steps_);
    along->addWidget(checkAlong_);
    if (links_->count() == 0) {
        links_->addItem(QStringLiteral("(no hay enlaces de movimiento)"));
        links_->setEnabled(false);
        span_->setEnabled(false);
        steps_->setEnabled(false);
        checkAlong_->setEnabled(false);
    }
    layout->addLayout(along);

    auto* buttons = new QHBoxLayout();
    auto* again = new QPushButton(QStringLiteral("Revisar ahora"));
    auto* close = new QPushButton(QStringLiteral("Cerrar"));
    close->setDefault(true);
    buttons->addWidget(again);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(again, &QPushButton::clicked, this, [this] { checkNow(); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(checkAlong_, &QPushButton::clicked, this, [this] {
        if (checkAlong_->text() == QStringLiteral("Parar")) {
            cancelRequested_ = true;
        } else {
            checkAlong();
        }
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { showSelected(); });
    fitDialogToScreen(this, QStringLiteral("interference"));
    checkNow();
}

InterferenceDialog::~InterferenceDialog()
{
    viewport_.hideGhost();
}

void InterferenceDialog::showPairs(const std::vector<document::InterferencePair>& pairs, bool along)
{
    pairs_ = pairs;
    table_->setRowCount(0);
    for (const document::InterferencePair& pair : pairs_) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, FirstColumn, new QTableWidgetItem(QString::fromStdString(pair.firstName)));
        table_->setItem(row, SecondColumn, new QTableWidgetItem(QString::fromStdString(pair.secondName)));
        table_->setItem(row, VolumeColumn, new QTableWidgetItem(QString::number(pair.volume, 'f', 2)));
        table_->setItem(row, AtColumn,
                        new QTableWidgetItem(along ? QString::number(pair.at * 180.0 / std::numbers::pi, 'f', 1) : QStringLiteral("—")));
    }
    viewport_.hideGhost();
    if (!pairs_.empty()) {
        table_->selectRow(0);
    }
}

void InterferenceDialog::checkNow()
{
    const auto pairs = document::findInterferences(document_.result(), 1e-3, hidden_);
    showPairs(pairs, false);
    summary_->setText(pairs.empty() ? QStringLiteral("Ningún cuerpo se pisa con otro en la posición de ahora (los que solo se tocan no cuentan).")
                                    : QStringLiteral("%1 pareja(s) de cuerpos se pisan. Elige una fila para ver en rojo lo que tienen en común.").arg(pairs.size()));
}

void InterferenceDialog::checkAlong()
{
    if (links_->currentData().isNull()) {
        return;
    }
    const document::FeatureId link = links_->currentData().toULongLong();
    checkAlong_->setText(QStringLiteral("Parar"));
    cancelRequested_ = false;
    summary_->setText(QStringLiteral("Revisando el giro…"));
    QApplication::processEvents();
    int checked = 0;
    const int steps = steps_->value();
    const auto pairs = document::findInterferencesAlong(
        document_, link, span_->value() * std::numbers::pi / 180.0, steps, &checked, 1e-3, [this, steps] {
            QApplication::processEvents();  // la interfaz sigue viva y el botón «Parar» se oye
            (void)steps;
            return cancelRequested_;
        });
    checkAlong_->setText(QStringLiteral("Revisar el giro"));
    showPairs(pairs, true);
    const QString where = QStringLiteral("%1 de %2 posiciones").arg(checked).arg(steps + 1);
    if (cancelRequested_) {
        summary_->setText(QStringLiteral("Revisión parada (se miraron %1).").arg(where));
    } else if (pairs.empty()) {
        summary_->setText(QStringLiteral("Sin interferencias en ninguna de las %1 revisadas.").arg(where));
    } else {
        summary_->setText(QStringLiteral("%1 pareja(s) se pisan en algún punto del giro (%2). Se muestra el peor momento de cada una.")
                              .arg(pairs.size()).arg(where));
    }
}

void InterferenceDialog::showSelected()
{
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty() || std::size_t(rows.front().row()) >= pairs_.size()) {
        viewport_.hideGhost();
        return;
    }
    viewport_.showGhost({pairs_[std::size_t(rows.front().row())].shape}, true);
}

} // namespace nuf::app
