# app: ejecutable con la interfaz Qt. Es el único lugar (junto con render/)
# que conoce Qt.
set(CMAKE_AUTOMOC ON)

add_executable(nuf_app
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.hpp
    src/SelfCheck.cpp
    src/SelfCheck.hpp)

set_target_properties(nuf_app PROPERTIES
    OUTPUT_NAME NufioCAD
    WIN32_EXECUTABLE ON)   # sin consola en Windows

nuf_set_warnings(nuf_app)
target_link_libraries(nuf_app PRIVATE
    Qt6::Widgets
    nuf::render
    nuf::commands
    nuf::document
    nuf::geometry
    nuf::io)
