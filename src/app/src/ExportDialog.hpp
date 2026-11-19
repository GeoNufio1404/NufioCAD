#pragma once

#include "BackgroundRun.hpp"

#include <nuf/document/Feature.hpp>
#include <nuf/geometry/Exchange.hpp>
#include <nuf/geometry/Printability.hpp>

#include <QDialog>

#include <map>
#include <utility>
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
    enum class Format { Stl, Obj, Step, ThreeMf };
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
    // Cuenta los triángulos de los cuerpos marcados (en segundo plano; lo ya contado no se repite).
    void refreshTriangleCount();
    void showTriangleCount();
    // Revisión para imprimir de los cuerpos marcados (solo con mallas), en segundo plano.
    void runReview();
    void showReview(const std::vector<geometry::PrintIssue>& issues, const std::string& failure);
    void savePrinterSettings() const;

    const std::vector<document::Body>& bodies_;
    QComboBox* format_ = nullptr;
    QComboBox* quality_ = nullptr;
    QDoubleSpinBox* customLinear_ = nullptr;   // calidad personalizada: desviación lineal (mm)
    QDoubleSpinBox* customAngular_ = nullptr;  // y angular (grados)
    QListWidget* list_ = nullptr;
    QCheckBox* onePerBody_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* review_ = nullptr;
    QWidget* printer_ = nullptr;
    QDoubleSpinBox* nozzle_ = nullptr;
    QDoubleSpinBox* bedX_ = nullptr;
    QDoubleSpinBox* bedY_ = nullptr;
    QDoubleSpinBox* bedZ_ = nullptr;
    QDoubleSpinBox* overhang_ = nullptr;
    QTimer* reviewTimer_ = nullptr;
    QLineEdit* slicer_ = nullptr;
    Mode mode_ = Mode::Export;
    QPushButton* ok_ = nullptr;

    // Mallar y revisar tarda (más con roscas o engranajes): va en hilos de trabajo para que marcar un cuerpo o
    // cambiar la calidad no pare la ventana.
    BackgroundRun reviewRun_;
    BackgroundRun countRun_;
    bool countAgain_ = false;  // la selección cambió mientras se contaba
    // Triángulos ya contados por (forma, calidad): marcar o desmarcar un cuerpo no vuelve a mallar los demás.
    std::map<std::pair<const void*, int>, std::size_t> triangles_;
};

} // namespace nuf::app
