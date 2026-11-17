#pragma once

#include <nuf/document/Document.hpp>
#include <nuf/io/DocumentFile.hpp>

#include <QDateTime>
#include <QLockFile>
#include <QString>

#include <atomic>
#include <filesystem>
#include <memory>
#include <thread>
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
    ~Recovery();
    Recovery(const Recovery&) = delete;
    Recovery& operator=(const Recovery&) = delete;

    // Guarda la copia (reemplaza la anterior). Lo rápido (armar el JSON) se hace aquí y lo lento (escribir en
    // SQLite y en disco, que con OneDrive o un antivirus puede tardar) en un hilo aparte, así que la
    // interfaz no espera. Si la copia anterior aún se está escribiendo, se salta esta vuelta (se reintenta
    // en la siguiente). Devuelve false si se saltó.
    bool writeAsync(const document::Document& document, const io::ViewState& view, const QString& original);
    // Borra la copia (el diseño se guardó o se cerró bien). Si se está escribiendo una, espera a que termine
    // para que no reaparezca después.
    void discard();

    // Copias de sesiones que ya no existen, la más reciente primero.
    static std::vector<Orphan> orphans();
    static void remove(const Orphan& orphan);

private:
    void finishWriting();

    QString id_;
    std::unique_ptr<QLockFile> lock_;
    std::thread writer_;
    std::atomic<bool> writing_{false};
};

} // namespace nuf::app
