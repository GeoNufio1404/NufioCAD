#pragma once

#include <nuf/document/Feature.hpp>
#include <nuf/geometry/Exchange.hpp>

#include <QDialog>

#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QListWidget;

namespace nuf::app {

// Archivo > Exportar: formato, cuerpos y calidad de la malla.
class ExportDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Format { Stl, Obj, Step };

    // `bodies` son los cuerpos del modelo; `checkedKeys` los que se marcan al abrir.
    ExportDialog(const std::vector<document::Body>& bodies, const std::vector<std::string>& checkedKeys,
                 QWidget* parent);

    Format format() const;
    geometry::MeshQuality quality() const;
    bool onePerBody() const;
    // Posiciones (en `bodies`) de los cuerpos marcados.
    std::vector<std::size_t> selected() const;

    // Extensión sin punto: «stl», «obj» o «step».
    static QString extension(Format format);

private:
    void updateSummary();
    std::vector<geometry::Shape> selectedShapes() const;

    const std::vector<document::Body>& bodies_;
    QComboBox* format_ = nullptr;
    QComboBox* quality_ = nullptr;
    QListWidget* list_ = nullptr;
    QCheckBox* onePerBody_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace nuf::app
