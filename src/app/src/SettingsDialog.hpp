#pragma once

#include <QDialog>

class QComboBox;

namespace nuf::app {

// Configuración de la aplicación: por ahora, la unidad de longitud con la que se muestra todo.
class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QComboBox* unit_ = nullptr;
};

} // namespace nuf::app
