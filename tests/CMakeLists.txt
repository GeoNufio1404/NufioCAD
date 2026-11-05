# Pruebas del núcleo: no dependen de Qt ni abren ventanas.
add_executable(nuf_tests
    test_signal.cpp
    test_command_stack.cpp
    test_geometry.cpp
    test_exchange.cpp
    test_modeling.cpp
    test_edge_ops.cpp
    test_document_model.cpp
    test_nuf_file.cpp
    test_document_file.cpp
    test_sketch.cpp
    test_sketch_pick.cpp
    test_sketch_plane.cpp
    test_sketch_profiles.cpp
    test_sketch_selection.cpp
    test_sketch_curves.cpp
    test_sketch_dxf.cpp
    test_sketch_edit.cpp
    test_sketch_tools.cpp
    test_sketch_tools_curves.cpp
    test_sketch_tools_2b.cpp)

nuf_set_warnings(nuf_tests)
target_compile_definitions(nuf_tests PRIVATE NUF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
target_link_libraries(nuf_tests PRIVATE
    nuf::document
    nuf::commands
    nuf::geometry
    nuf::io
    nuf::sketch
    Catch2::Catch2WithMain)

include(Catch)
if(WIN32)
    # En Windows ctest pasa el nombre de cada prueba con la página de códigos
    # del sistema, no en UTF-8, y Catch2 no encuentra las que llevan tildes:
    # se ejecuta el binario entero como una sola prueba.
    add_test(NAME nuf_tests COMMAND nuf_tests)
else()
    # PRE_TEST: descubre las pruebas al correr ctest.
    catch_discover_tests(nuf_tests DISCOVERY_MODE PRE_TEST)
endif()
