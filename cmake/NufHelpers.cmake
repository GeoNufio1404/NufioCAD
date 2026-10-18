# Funciones comunes para todos los módulos del proyecto.

# Advertencias estrictas y equivalentes en MSVC y GCC/Clang.
function(nuf_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive-
            $<$<BOOL:${NUF_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
            $<$<BOOL:${NUF_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

# Crea una biblioteca del núcleo:
#   nuf_add_library(geometry SOURCES src/Kernel.cpp)
# Genera el target nuf_geometry, su alias nuf::geometry y expone
# include/ como cabeceras públicas (#include <nuf/geometry/...>).
function(nuf_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    set(target nuf_${name})
    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        target_include_directories(${target} PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        nuf_set_warnings(${target})
    else()
        add_library(${target} INTERFACE)
        target_include_directories(${target} INTERFACE
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
    endif()
    add_library(nuf::${name} ALIAS ${target})
endfunction()
