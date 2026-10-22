# PlaneGCS: solver de restricciones 2D del sketcher de FreeCAD (LGPL-2.1+).
# Copia con parches mínimos; ver README.md. No se aplican las advertencias
# estrictas del proyecto: es código de terceros.
find_package(Eigen3 REQUIRED NO_MODULE)
find_package(Threads REQUIRED)

add_library(planegcs STATIC
    src/Constraints.cpp
    src/GCS.cpp
    src/Geo.cpp
    src/SubSystem.cpp
    src/qp_eq.cpp)
add_library(planegcs::planegcs ALIAS planegcs)

target_include_directories(planegcs SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/compat")
target_link_libraries(planegcs PUBLIC Eigen3::Eigen Threads::Threads)
# Usa std::unreachable (C++23); quien lo use solo necesita C++20.
set_target_properties(planegcs PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_compile_features(planegcs INTERFACE cxx_std_20)

set(prelude "${CMAKE_CURRENT_SOURCE_DIR}/compat/PlaneGcsPrelude.h")
if(MSVC)
    target_compile_definitions(planegcs PUBLIC _USE_MATH_DEFINES NOMINMAX)
    target_compile_options(planegcs PRIVATE /bigobj "/FI${prelude}")
else()
    target_compile_options(planegcs PRIVATE -include "${prelude}")
endif()
