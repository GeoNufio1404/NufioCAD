# sketch: geometría 2D, restricciones y solver (PlaneGCS detrás de ISolver).
nuf_add_library(sketch SOURCES
    src/Plane.cpp
    src/Sketch.cpp
    src/Snap.cpp
    src/Tools.cpp
    src/PlaneGcsSolver.cpp)

target_link_libraries(nuf_sketch PRIVATE planegcs::planegcs)
