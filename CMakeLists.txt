cmake_minimum_required(VERSION 3.25)

# NufioCAD (nombre de trabajo). Internamente el código usa el prefijo corto
# "nuf" (namespaces, targets), igual que la extensión de archivo .nuf.
project(NufioCAD
    VERSION 0.1.0
    DESCRIPTION "CAD paramétrico de escritorio (Windows y Linux)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NUF_BUILD_APP "Compilar la interfaz (Qt) además del núcleo" ON)
option(NUF_BUILD_TESTS "Compilar las pruebas del núcleo" ON)
option(NUF_WARNINGS_AS_ERRORS "Tratar las advertencias como errores" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NufHelpers)

if(MSVC)
    # Código fuente y cadenas en UTF-8 en Windows
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
# Dependencias del núcleo (sin Qt)
# ---------------------------------------------------------------------------
find_package(OpenCASCADE CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
message(STATUS "OpenCASCADE ${OpenCASCADE_VERSION}")
message(STATUS "SQLite ${SQLite3_VERSION}")

add_subdirectory(third_party/planegcs)

add_subdirectory(src/geometry)
add_subdirectory(src/sketch)
add_subdirectory(src/document)
add_subdirectory(src/commands)
add_subdirectory(src/io)

# ---------------------------------------------------------------------------
# Interfaz (Qt solo vive aquí)
# ---------------------------------------------------------------------------
if(NUF_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
    message(STATUS "Qt ${Qt6_VERSION}")
    add_subdirectory(src/render)
    add_subdirectory(src/app)
endif()

# ---------------------------------------------------------------------------
# Pruebas
# ---------------------------------------------------------------------------
if(NUF_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 REQUIRED)
    add_subdirectory(tests)
endif()
