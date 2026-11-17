# app: ejecutable con la interfaz Qt. Es el único lugar (junto con render/)
# que conoce Qt.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(nuf_app
    src/main.cpp
    src/BackgroundRun.cpp
    src/BackgroundRun.hpp
    src/MainWindow.cpp
    src/MainWindowMenus.cpp
    src/MainWindowView.cpp
    src/MainWindowCommon.hpp
    src/MainWindowFiles.cpp
    src/MainWindow.hpp
    src/FeatureTools.cpp
    src/FeatureToolsProfile.cpp
    src/FeatureToolsConstruct.cpp
    src/FeatureToolsMore.cpp
    src/FeatureToolsCommon.hpp
    src/FeatureTools.hpp
    src/ModelPanels.cpp
    src/ModelPanels.hpp
    src/SelfCheck.cpp
    src/SketchSession.cpp
    src/SketchSession.hpp
    src/ValueInput.cpp
    src/ValueInput.hpp
    src/SelfCheck.hpp
    src/Theme.cpp
    src/Theme.hpp
    src/ExportDialog.cpp
    src/ExportDialog.hpp
    src/ExpressionSpinBox.cpp
    src/InterfaceCheck.cpp
    src/InterfaceCheck.hpp
    src/GeneratorTool.cpp
    src/GeneratorTool.hpp
    src/SettingsDialog.cpp
    src/SettingsDialog.hpp
    src/Units.cpp
    src/Units.hpp
    src/ExpressionSpinBox.hpp
    src/ParametersDialog.cpp
    src/ParametersDialog.hpp
    src/BodiesDialog.cpp
    src/BodiesDialog.hpp
    src/Icons.cpp
    src/NavBar.cpp
    src/NavBar.hpp
    src/Recovery.cpp
    src/Recovery.hpp
    src/StatusBanner.cpp
    src/StatusBanner.hpp
    src/BusySpinner.cpp
    src/BusySpinner.hpp
    src/StallWatch.cpp
    src/StallWatch.hpp
    src/CrashHandler.cpp
    src/CrashHandler.hpp
    src/Ribbon.cpp
    src/StartScreen.cpp
    src/StartScreen.hpp
    src/Ribbon.hpp
    src/Icons.hpp
    resources/resources.qrc)

if(WIN32)
    # Icono del .exe (el de la ventana lo asigna main.cpp desde los recursos Qt).
    target_sources(nuf_app PRIVATE resources/nufiocad.rc)
endif()

set_target_properties(nuf_app PROPERTIES
    OUTPUT_NAME NufioCAD
    WIN32_EXECUTABLE ON)   # sin consola en Windows

nuf_set_warnings(nuf_app)
target_link_libraries(nuf_app PRIVATE
    Qt6::Widgets
    Qt6::Svg
    nuf::render
    nuf::commands
    nuf::document
    nuf::geometry
    nuf::io
    nuf::sketch)
if(WIN32)
    target_link_libraries(nuf_app PRIVATE dbghelp)
endif()

# Instalación (Linux): ejecutable, entrada de menú, iconos y tipo MIME de .nuf.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS nuf_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/nufiocad.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/nufiocad-mime.xml"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME nufiocad.xml)
    foreach(size 16 24 32 48 64 128 256)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app/nufiocad-${size}.png"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME nufiocad.png)
    endforeach()
endif()
