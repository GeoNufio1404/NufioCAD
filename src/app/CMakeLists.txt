# app: ejecutable con la interfaz Qt. Es el único lugar (junto con render/)
# que conoce Qt.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(nuf_app
    src/main.cpp
    src/BackgroundRun.cpp
    src/BackgroundRun.hpp
    src/MainWindow.cpp
    src/RadialMenu.cpp
    src/MainWindowMenus.cpp
    src/MainWindowView.cpp
    src/MainWindowCommon.hpp
    src/MainWindowFiles.cpp
    src/MainWindowAssembly.cpp
    src/InterferenceDialog.cpp
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
# La versión sale de project(... VERSION) para que el instalador, el programa y la ventana «Acerca de» digan lo mismo.
target_compile_definitions(nuf_app PRIVATE NUF_VERSION="${PROJECT_VERSION}")
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

# Instalación (Windows, D1): el ejecutable en bin/ con todas las DLL de las que depende (OpenCASCADE y demás, las que vcpkg
# deja junto al ejecutable) y, con windeployqt, las DLL y los plugins de Qt; más los avisos de licencias de terceros.
# `cmake --install build/windows-release --prefix <carpeta>` deja una copia lista para ejecutar; `cpack` la empaqueta.
if(WIN32)
    include(GNUInstallDirs)
    install(TARGETS nuf_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    # Las DLL de vcpkg (OpenCASCADE y lo que éste necesita: FreeType, libpng, zlib, bzip2, Brotli, SQLite…). TARGET_RUNTIME_DLLS no
    # trae las de segundo nivel (sin ellas el programa se queda esperando en un aviso de «falta una DLL» sin ventana).
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET AND EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
        install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
                DESTINATION ${CMAKE_INSTALL_BINDIR} FILES_MATCHING PATTERN "*.dll")
    endif()
    install(FILES $<TARGET_RUNTIME_DLLS:nuf_app> DESTINATION ${CMAKE_INSTALL_BINDIR})
    qt_generate_deploy_app_script(
        TARGET nuf_app
        OUTPUT_SCRIPT nuf_qt_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        # Sin vc_redist.x64.exe: windeployqt lo copiaba pero nadie lo ejecutaba, y sin esas bibliotecas el programa no arranca.
        DEPLOY_TOOL_OPTIONS --no-compiler-runtime)
    install(SCRIPT ${nuf_qt_deploy_script})
    # Las bibliotecas de Microsoft (vcruntime140, msvcp140…) van junto al programa: no hay que instalar nada más, tampoco con el ZIP.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(CMAKE_INSTALL_UCRT_LIBRARIES OFF)  # el resto (UCRT) viene con Windows 10 y 11
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(WARNING "No se encontraron las bibliotecas de Microsoft: el paquete no arrancaría en un Windows sin ellas")
    endif()
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/AVISOS-DE-TERCEROS.txt"
            DESTINATION .)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION . RENAME LICENSE.txt)
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/planegcs/LICENSE"
            DESTINATION licencias RENAME PlaneGCS-LICENSE.txt)
endif()

# Instalación (Linux): ejecutable, entrada de menú, iconos y tipo MIME de .nuf.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS nuf_app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/nufiocad.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/nufiocad-mime.xml"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME nufiocad.xml)
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/AVISOS-DE-TERCEROS.txt"
            DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(FILES "${PROJECT_SOURCE_DIR}/third_party/planegcs/LICENSE"
            DESTINATION ${CMAKE_INSTALL_DOCDIR}/licencias RENAME PlaneGCS-LICENSE.txt)
    foreach(size 16 24 32 48 64 128 256)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app/nufiocad-${size}.png"
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME nufiocad.png)
    endforeach()
endif()
