#pragma once

#include <QFrame>
#include <QString>

#include <optional>
#include <vector>

class QLabel;
class QLineEdit;

namespace nuf::app {

// Recuadro junto al cursor para escribir valores mientras se dibuja
// ("Longitud", "Ángulo"…), como en Fusion 360. Muestra el valor actual como
// texto de ayuda; al escribir, Tab pasa al siguiente campo y Enter confirma.
class ValueInput final : public QFrame {
    Q_OBJECT

public:
    explicit ValueInput(QWidget* parent);

    // Campos y valores actuales (se muestran mientras no se escriba nada).
    void setFields(const std::vector<QString>& labels, const std::vector<double>& current);
    // Empieza a escribir en el primer campo con «text» (la primera tecla).
    void startTyping(const QString& text);
    bool isTyping() const;
    // Vacía lo escrito.
    void clear();
    // Valores escritos; nullopt en los campos vacíos o no numéricos.
    std::vector<std::optional<double>> values() const;

signals:
    void accepted();

private:
    std::vector<QString> labels_;
    std::vector<QLabel*> captions_;
    std::vector<QLineEdit*> edits_;
};

} // namespace nuf::app
