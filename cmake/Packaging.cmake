# Empaquetado (Fase 19, D1): `cpack --config build/<preset>/CPackConfig.cmake`.
#   Windows: instalador NSIS (NufioCAD-<versión>-win64.exe) y ZIP portátil, con las DLL de Qt, de OpenCASCADE y de todo lo que
#            enlaza, los plugins de Qt (windeployqt), la asociación de .nuf, el acceso del menú Inicio, el desinstalador y los
#            avisos de licencias de terceros.
#   Linux:   .tar.gz, .deb y .rpm con el ejecutable, la entrada de menú, los iconos y el tipo MIME; el AppImage se hace con
#            packaging/linux/crear-appimage.sh.
# Los pasos completos (y cómo firmar) están en docs/distribucion.md.

if(NOT NUF_BUILD_APP)
    return()
endif()

set(CPACK_PACKAGE_NAME "NufioCAD")
set(CPACK_PACKAGE_VENDOR "NufioCAD")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CAD paramétrico de escritorio para diseñar piezas e imprimirlas en 3D")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/GeoNufio1404/NufioCAD")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NufioCAD")
set(CPACK_PACKAGE_CONTACT "NufioCAD <https://github.com/GeoNufio1404/NufioCAD/issues>")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "NufioCAD-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-x64")

if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "NufioCAD-${PROJECT_VERSION}-win64")
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_EXECUTABLES "NufioCAD" "NufioCAD")          # acceso en el menú Inicio (el ejecutable está en bin/)
    set(CPACK_CREATE_DESKTOP_LINKS "NufioCAD")
    set(CPACK_NSIS_DISPLAY_NAME "NufioCAD ${PROJECT_VERSION}")
    set(CPACK_NSIS_PACKAGE_NAME "NufioCAD")
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/src/app/resources/nufiocad.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/src/app/resources/nufiocad.ico")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\NufioCAD.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/GeoNufio1404/NufioCAD")
    set(CPACK_NSIS_HELP_LINK "https://github.com/GeoNufio1404/NufioCAD/issues")
    # Asociación de los archivos .nuf (con su icono y «Abrir con NufioCAD») y su limpieza al desinstalar.
    # Las órdenes van entre corchetes (sin escapes de CMake) y con comillas simples de NSIS: con comillas dobles escapadas,
    # CPack las estropeaba al pasarlas al guion del instalador y NSIS no lo compilaba.
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [=[
        WriteRegStr SHCTX 'Software\Classes\.nuf' '' 'NufioCAD.Documento'
        WriteRegStr SHCTX 'Software\Classes\NufioCAD.Documento' '' 'Diseño de NufioCAD'
        WriteRegStr SHCTX 'Software\Classes\NufioCAD.Documento\DefaultIcon' '' '$INSTDIR\bin\NufioCAD.exe,0'
        WriteRegStr SHCTX 'Software\Classes\NufioCAD.Documento\shell\open\command' '' '"$INSTDIR\bin\NufioCAD.exe" "%1"'
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ]=])
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [=[
        DeleteRegKey SHCTX 'Software\Classes\.nuf'
        DeleteRegKey SHCTX 'Software\Classes\NufioCAD.Documento'
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ]=])
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_RPM_PACKAGE_LICENSE "MIT")
    set(CPACK_RPM_PACKAGE_AUTOREQ ON)
    set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
endif()

# Firma de los binarios antes de empaquetar (solo con NUF_FIRMAR=1; ver cmake/FirmarPaquete.cmake y docs/distribucion.md).
set(CPACK_PRE_BUILD_SCRIPTS "${PROJECT_SOURCE_DIR}/cmake/FirmarPaquete.cmake")

include(CPack)
