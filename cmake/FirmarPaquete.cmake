# Se ejecuta antes de que CPack arme el paquete (CPACK_PRE_BUILD_SCRIPTS): firma los .exe y .dll de la carpeta temporal del
# paquete, así el instalador lleva los binarios firmados. Solo actúa con NUF_FIRMAR=1 en el entorno y en Windows; sin
# certificado, packaging/windows/firmar.ps1 no hace nada. El propio instalador se firma después (ver docs/distribucion.md).
if(WIN32 AND "$ENV{NUF_FIRMAR}" STREQUAL "1")
    message(STATUS "Firmando los binarios del paquete en ${CPACK_TEMPORARY_DIRECTORY}")
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/firmar.ps1"
                -Ruta "${CPACK_TEMPORARY_DIRECTORY}"
        RESULT_VARIABLE firma_resultado)
    if(NOT firma_resultado EQUAL 0)
        message(FATAL_ERROR "No se pudieron firmar los binarios del paquete")
    endif()
endif()
