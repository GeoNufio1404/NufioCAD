# sketch: geometría 2D, restricciones y solver (PlaneGCS detrás de ISolver).
nuf_add_library(sketch SOURCES
    src/Annotations.cpp
    src/FromSelection.cpp
    src/Pick.cpp
    src/Plane.cpp
    src/Profiles.cpp
    src/Sketch.cpp
    src/Snap.cpp
    src/ToolSupport.cpp
    src/Tools.cpp
    src/ShapeTools.cpp
    src/CircleTools.cpp
    src/PlaneGcsSolver.cpp)

target_link_libraries(nuf_sketch PRIVATE planegcs::planegcs)
