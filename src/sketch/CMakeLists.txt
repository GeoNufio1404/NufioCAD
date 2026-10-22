# sketch: geometría 2D, restricciones y solver (PlaneGCS detrás de ISolver).
nuf_add_library(sketch SOURCES
    src/Sketch.cpp
    src/PlaneGcsSolver.cpp)

target_link_libraries(nuf_sketch PRIVATE planegcs::planegcs)
