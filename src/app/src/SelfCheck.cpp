#include "SelfCheck.hpp"

#include <nuf/geometry/Kernel.hpp>
#include <nuf/io/NufFile.hpp>

#include <QtGlobal>

#include <cmath>
#include <exception>
#include <filesystem>
#include <string>

namespace nuf::app {

int runSelfCheck(std::ostream& out)
{
    try {
        out << "Qt         " << qVersion() << '\n';
        out << "OCCT       " << geometry::kernelVersion() << '\n';
        out << "SQLite     " << io::NufFile::sqliteVersion() << '\n';

        const double volume = geometry::probeBoxVolume(10.0, 20.0, 30.0);
        out << "Caja 10x20x30 mm -> volumen " << volume << " mm3\n";
        if (std::abs(volume - 6000.0) > 1e-6) {
            out << "ERROR: volumen inesperado\n";
            return 1;
        }

        // Ruta con acentos a propósito: verifica el manejo de UTF-8.
        const auto path = std::filesystem::temp_directory_path() / u8"nuf_autoprueba_diseño.nuf";
        std::filesystem::remove(path);
        {
            auto file = io::NufFile::create(path);
            file.setMeta("unidades", "mm");
        }
        bool ok = false;
        {
            // Cerrado antes de borrarlo: Windows no deja borrar un archivo abierto.
            const auto reopened = io::NufFile::open(path);
            ok = reopened.meta("unidades") == std::string("mm");
        }
        std::filesystem::remove(path);
        out << "Archivo .nuf: " << (ok ? "crear y reabrir OK" : "ERROR") << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& error) {
        out << "ERROR: " << error.what() << '\n';
        return 1;
    }
}

} // namespace nuf::app
