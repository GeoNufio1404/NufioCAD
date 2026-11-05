#pragma once

#include <nuf/document/Feature.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Printability.hpp>

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QTimer;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;

namespace nuf::app {

// Archivo > Exportar: formato, cuerpos y calidad de la malla.
class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Format { Stl, Obj, Step };
    // Exportar a un archivo, o Imprimir: lo mismo pero siempre STL y, al aceptar, se abre en el slicer.
    enum class Mode { Export, Print };

    // `bodies` son los cuerpos del modelo; `checkedKeys` los que se marcan al abrir.
    ExportDialog(const std::vector<document::Body>& bodies, const std::vector<std::string>& checkedKeys,
                 QWidget* parent, Mode mode = Mode::Export);

    Format format() const;
    geometry::MeshQuality quality() const;
    bool onePerBody() const;
    // El programa de laminado elegido (solo en el modo Imprimir).
    QString slicerPath() const;
    // Las medidas de la impresora (boquilla y cama) que se usan en la revisión.
    geometry::PrintSettings printSettings() const;
    // Posiciones (en `bodies`) de los cuerpos marcados.
    std::vector<std::size_t> selected() const;

    // Extensión sin punto: «stl», «obj» o «step».
    static QString extension(Format format);

private:
    void updateSummary();
    // Revisión para imprimir de los cuerpos marcados (solo con mallas).
    void runReview();
    void savePrinterSettings() const;
    std::vector<geometry::Shape> selectedShapes() const;

    const std::vector<document::Body>& bodies_;
    QComboBox* format_ = nullptr;
    QComboBox* quality_ = nullptr;
    QListWidget* list_ = nullptr;
    QCheckBox* onePerBody_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* review_ = nullptr;
    QWidget* printer_ = nullptr;
    QDoubleSpinBox* nozzle_ = nullptr;
    QDoubleSpinBox* bedX_ = nullptr;
    QDoubleSpinBox* bedY_ = nullptr;
    QDoubleSpinBox* bedZ_ = nullptr;
    QTimer* reviewTimer_ = nullptr;
    QLineEdit* slicer_ = nullptr;
    Mode mode_ = Mode::Export;
    QPushButton* ok_ = nullptr;
};

} // namespace nuf::app
