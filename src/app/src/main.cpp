#include "MainWindow.hpp"
#include "SelfCheck.hpp"
#include "Theme.hpp"

#include <QApplication>
#include <QIcon>

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
    // El tema gtk3 (predeterminado en GNOME) no informa la preferencia de
    // modo oscuro; el portal de escritorio sí, y la sigue en vivo.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationDisplayName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    // Varios tamaños: X11 descarta los iconos demasiado grandes (1024 px).
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/app/%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(icon);

    nuf::app::initTheme(app);

    nuf::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
