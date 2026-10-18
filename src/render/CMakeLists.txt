# render: viewport 3D. En la Fase 1 se reemplaza el marcador de posición
# por un QOpenGLWidget con el visor V3d/AIS de OpenCASCADE.
nuf_add_library(render SOURCES src/Viewport.cpp)
target_link_libraries(nuf_render PUBLIC Qt6::Widgets)
