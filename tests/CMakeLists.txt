# Pruebas del núcleo: no dependen de Qt ni abren ventanas.
add_executable(nuf_tests
    test_signal.cpp
    test_command_stack.cpp
    test_geometry.cpp
    test_nuf_file.cpp
    test_sketch.cpp
    test_sketch_pick.cpp
    test_sketch_plane.cpp
    test_sketch_profiles.cpp
    test_sketch_selection.cpp
    test_sketch_tools.cpp
    test_sketch_tools_2b.cpp)

nuf_set_warnings(nuf_tests)
target_link_libraries(nuf_tests PRIVATE
    nuf::document
    nuf::commands
    nuf::geometry
    nuf::io
    nuf::sketch
    Catch2::Catch2WithMain)

include(Catch)
# PRE_TEST: descubre las pruebas al correr ctest (en Windows las DLL de
# las dependencias ya están copiadas para entonces).
catch_discover_tests(nuf_tests DISCOVERY_MODE PRE_TEST)
