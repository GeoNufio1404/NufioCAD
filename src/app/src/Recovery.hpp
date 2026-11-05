#pragma once

#include <nuf/document/Document.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <QDateTime>
#include <QLockFile>
#include <QString>

#include <filesystem>
#include <memory>
#include <vector>

namespace nuf::app {

// Copia de recuperación del diseño abierto. Vive en la carpeta de datos de la
// aplicación (nunca junto al archivo del usuario, que no se toca). Cada
// ventana tiene la suya, con un archivo de bloqueo: si el proceso muere sin
// borrarla, la siguiente ejecución la encuentra «huérfana» y la ofrece.
class Recovery {
public:
    struct Orphan {
        QString id;
        std::filesystem::path file;  // la copia (.nuf)
        QString original;            // ruta del diseño de origen; vacía si nunca se guardó
        QString name;
        QDateTime when;
    };

    Recovery();

    // Guarda la copia (reemplaza la anterior). Devuelve false si falló: no es
    // grave, se reintenta en la siguiente vuelta.
    bool write(const document::Document& document, const io::ViewState& view, const QString& original);
    // Borra la copia (el diseño se guardó o se cerró bien).
    void discard();

    // Copias de sesiones que ya no existen, la más reciente primero.
    static std::vector<Orphan> orphans();
    static void remove(const Orphan& orphan);

private:
    QString id_;
    std::unique_ptr<QLockFile> lock_;
};

} // namespace nuf::app
