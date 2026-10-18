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

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationDisplayName(QStringLiteral("NufioCAD"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    nuf::app::MainWindow window;
    window.show();
    return QApplication::exec();
}
