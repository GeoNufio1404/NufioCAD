#pragma once

// Tabla de parámetros con nombre: «grosor = 3», «largo = grosor * 10». Muestra
// el valor de cada uno y avisa de los errores (nombre repetido, referencias
// circulares, parámetros que no existen…).

#include <nuf/document/Document.hpp>

#include <QDialog>

class QLabel;
class QPushButton;
class QTableWidget;

namespace nuf::app {

class ParametersDialog final : public QDialog {
    Q_OBJECT

public:
    ParametersDialog(const std::vector<document::Parameter>& parameters, QWidget* parent = nullptr);

    std::vector<document::Parameter> parameters() const;

private:
    void addRow(const QString& name, const QString& expression);
    // Recalcula los valores y activa «Aceptar» solo si todo está bien.
    void refresh();

    QTableWidget* table_ = nullptr;
    QLabel* problem_ = nullptr;
    QPushButton* ok_ = nullptr;
    bool refreshing_ = false;
};

} // namespace nuf::app
