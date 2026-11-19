#include "CrashHandler.hpp"
#include "InterfaceCheck.hpp"
#include "MainWindow.hpp"
#include "SelfCheck.hpp"
#include "StallWatch.hpp"
#include "Theme.hpp"

#include <nuf/io/DocumentFile.hpp>
#include <nuf/render/Viewport.hpp>

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

int main(int argc, char* argv[])
{
    // --dump archivo.nuf: imprime el diseño como JSON legible (depuración).
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--dump") {
            try {
                std::cout << nuf::io::dumpDocument(std::filesystem::path(argv[i + 1])) << '\n';
                return 0;
            } catch (const std::exception& e) {
                std::cerr << e.what() << '\n';
                return 1;
            }
        }
    }
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

    nuf::app::installCrashHandler();
    // --probar-cierre: provoca un fallo a propósito para comprobar el informe de cierres.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--probar-cierre") {
            *static_cast<volatile int*>(nullptr) = 1;
        }
    }
    // --prueba-interfaz [--informe archivo] [--umbral-ms N]: abre las herramientas con la ventana real y comprueba
    // que no hay fallos ni paradas largas. Usa carpetas y ajustes propios: no toca los del usuario.
    bool interfaceCheck = false;
    QString captureFolder;  // --captura-temas carpeta: capturas del tema claro y oscuro
    QString inventoryFolder;  // --inventario carpeta [--tamano WxH] [--escala F]: Fase 20, R1
    QSize requestedSize;
    QString reportPath;
    int thresholdMs = 20000;  // en Debug, OpenCASCADE es lento: sirve para detectar paradas de decenas de segundos
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument(argv[i]);
        if (argument == "--prueba-interfaz") {
            interfaceCheck = true;
        } else if (argument == "--captura-temas" && i + 1 < argc) {
            interfaceCheck = true;
            captureFolder = QString::fromLocal8Bit(argv[i + 1]);
        } else if (argument == "--inventario" && i + 1 < argc) {
            interfaceCheck = true;
            inventoryFolder = QString::fromLocal8Bit(argv[i + 1]);
        } else if (argument == "--tamano" && i + 1 < argc) {
            const QStringList parts = QString::fromLocal8Bit(argv[i + 1]).split(QLatin1Char('x'));
            if (parts.size() == 2) {
                requestedSize = QSize(parts[0].toInt(), parts[1].toInt());
            }
        } else if (argument == "--escala" && i + 1 < argc) {
            qputenv("QT_SCALE_FACTOR", QByteArray(argv[i + 1]));  // antes de crear la aplicación
        } else if (argument == "--informe" && i + 1 < argc) {
            reportPath = QString::fromLocal8Bit(argv[i + 1]);
        } else if (argument == "--umbral-ms" && i + 1 < argc) {
            thresholdMs = QString::fromLocal8Bit(argv[i + 1]).toInt();
        }
    }
    if (interfaceCheck) {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        // Cada prueba parte de cero: lo que una vez recordaron los generadores (cada paso acepta valores cambiados)
        // deformaba las cajas de las pruebas siguientes.
        QDir(QDir::tempPath() + QStringLiteral("/NufioCAD-prueba")).removeRecursively();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir::tempPath() + QStringLiteral("/NufioCAD-prueba"));
        if (reportPath.isEmpty()) {
            reportPath = QDir::tempPath() + QStringLiteral("/NufioCAD-prueba-interfaz.txt");
        }
    }
    QSurfaceFormat::setDefaultFormat(nuf::render::Viewport::defaultFormat());
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationDisplayName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationVersion(QStringLiteral(NUF_VERSION));
    // Une la ventana con nufiocad.desktop (icono del dock y del menú).
    QGuiApplication::setDesktopFileName(QStringLiteral("nufiocad"));
    // Varios tamaños: X11 descarta los iconos demasiado grandes (1024 px).
    QIcon icon;
    for (int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QStringLiteral(":/icons/app/%1.png").arg(size), QSize(size, size));
    }
    QApplication::setWindowIcon(icon);

    nuf::app::initTheme(app);

    nuf::app::MainWindow window;
    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        window.resize(requestedSize);
        window.adaptToSize();
    }
    window.show();
    if (interfaceCheck) {
        QTimer::singleShot(800, &window, [&window, reportPath, thresholdMs, captureFolder, inventoryFolder] {
            nuf::app::InterfaceCheck check(window, reportPath, thresholdMs);
            const int code = !inventoryFolder.isEmpty() ? check.captureInventory(inventoryFolder)
                             : captureFolder.isEmpty() ? check.run() : check.captureThemes(captureFolder);
            window.close();
            QApplication::exit(code);
        });
        return QApplication::exec();
    }
    // NUF_VIGILANTE=1 (o un número de ms): anota en cierres/paradas.txt cada vez que la interfaz se queda parada
    // más del umbral, con la pila de llamadas (Windows). Es para encontrar, usando la aplicación, lo que la congela.
    std::unique_ptr<nuf::app::StallWatch> stallWatch;
    if (const int stallMs = nuf::app::StallWatch::thresholdFromEnvironment(); stallMs > 0) {
        stallWatch = std::make_unique<nuf::app::StallWatch>(stallMs, nuf::app::StallWatch::defaultDirectory());
    }
    // NufioCAD archivo.nuf (también lo que pasa el sistema al abrir un .nuf).
    const QStringList arguments = QApplication::arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (!arguments[i].startsWith(QLatin1Char('-'))) {
            window.openFromCommandLine(arguments[i]);
            break;
        }
    }
    window.offerRecovery();
    return QApplication::exec();
}
