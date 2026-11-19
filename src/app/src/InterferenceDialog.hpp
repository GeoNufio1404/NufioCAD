#pragma once

// Inspeccionar › Interferencias (Fase 21, A8): los cuerpos que se pisan, con el volumen común (se ve en rojo en la
// vista al elegir una fila), en la posición de ahora y a lo largo del giro de un enlace de movimiento.

#include <nuf/document/Document.hpp>
#include <nuf/document/Interference.hpp>

#include <QDialog>

#include <set>
#include <string>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace nuf::render {
class Viewport;
}

namespace nuf::app {

class InterferenceDialog final : public QDialog {
    Q_OBJECT

public:
    InterferenceDialog(const document::Document& document, render::Viewport& viewport,
                       const std::set<std::string>& hiddenBodies, QWidget* parent = nullptr);
    ~InterferenceDialog() override;

private:
    void showPairs(const std::vector<document::InterferencePair>& pairs, bool along);
    void checkNow();
    void checkAlong();
    void showSelected();

    const document::Document& document_;
    render::Viewport& viewport_;
    std::set<std::string> hidden_;
    std::vector<document::InterferencePair> pairs_;
    QLabel* summary_ = nullptr;
    QTableWidget* table_ = nullptr;
    QComboBox* links_ = nullptr;
    QDoubleSpinBox* span_ = nullptr;
    QSpinBox* steps_ = nullptr;
    QPushButton* checkAlong_ = nullptr;
    bool cancelRequested_ = false;
};

} // namespace nuf::app
