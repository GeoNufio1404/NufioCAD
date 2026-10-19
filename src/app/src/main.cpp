#include "MainWindow.hpp"
#include "SelfCheck.hpp"

#include <QApplication>

#include <iostream>
#include <string_view>

int main(int argc, char* argv[])
{
    // --self-check: verifica Qt, OCCT y SQLite sin abrir ventanas.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--self-check") {
            return nuf::app::runSelfCheck(std::cout);
        }
    }

#ifdef Q_OS_LINUX
    // El OpenCASCADE de las distribuciones usa GLX (X11) y no puede adoptar
    // el contexto EGL que Qt crea en Wayland. Mientras tanto se usa XWayland,
    // salvo que el usuario elija otra plataforma con QT_QPA_PLATFORM.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && !qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationDisplayName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    nuf::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
