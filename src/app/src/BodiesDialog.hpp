#pragma once

// Nombres propios de los cuerpos y componentes (grupos): «Tapa», «Base»\u2026 y a
// qu\u00e9 componente pertenece cada uno («Carcasa»). Los componentes aparecen como
// carpetas en el navegador y se pueden mostrar u ocultar de una vez.

#include <nuf/document/Document.hpp>

#include <QDialog>

class QTableWidget;

namespace nuf::app {

class BodiesDialog final : public QDialog {
    Q_OBJECT

public:
    BodiesDialog(const document::Document& document, QWidget* parent = nullptr);

    // Todo lo que tenía el documento, con los cambios hechos aquí.
    std::map<std::string, document::BodyInfo> info() const;

private:
    std::map<std::string, document::BodyInfo> original_;
    std::vector<std::string> keys_;  // una por fila
    QTableWidget* table_ = nullptr;
};

} // namespace nuf::app
