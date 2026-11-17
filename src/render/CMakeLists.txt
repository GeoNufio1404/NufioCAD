# render: viewport 3D con el visor V3d/AIS de OpenCASCADE dentro de un
# QOpenGLWidget. Junto con geometry/, es el único módulo que incluye OCCT.
nuf_add_library(render SOURCES
    src/Viewport.cpp
    src/ViewportBodies.cpp
    src/ViewportHandles.cpp
    src/ViewportSketch.cpp
    src/ViewportSketchDraw.cpp
    src/ViewportScene.cpp
    src/ViewportImpl.hpp
    src/ViewportDrawing.hpp
    include/nuf/render/Viewport.hpp)   # listada para que AUTOMOC la procese

set_target_properties(nuf_render PROPERTIES AUTOMOC ON)
target_include_directories(nuf_render SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(nuf_render
    PUBLIC Qt6::Widgets Qt6::OpenGLWidgets nuf::document
    PRIVATE
        TKernel TKMath TKG3d TKGeomBase TKBRep TKPrim TKTopAlgo TKBO
        TKService TKV3d TKOpenGl)

if(WIN32)
    target_compile_definitions(nuf_render PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(nuf_render PRIVATE opengl32)   # wglGetCurrentDC
endif()
