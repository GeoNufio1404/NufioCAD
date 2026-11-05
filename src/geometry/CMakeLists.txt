# geometry: único módulo que habla directamente con OpenCASCADE (render/
# también, para dibujar; usa nuf/geometry/Occt.hpp como puente).
nuf_add_library(geometry SOURCES
    src/Exchange.cpp
    src/Kernel.cpp
    src/Modeling.cpp
    src/Printability.cpp
    src/Shape.cpp
    src/SketchFaces.cpp)

target_include_directories(nuf_geometry SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(nuf_geometry
    PUBLIC nuf::sketch
    PRIVATE
        TKernel TKMath TKG2d TKG3d TKGeomBase TKGeomAlgo TKBRep TKPrim TKTopAlgo
        TKBO TKFeat TKFillet TKOffset TKShHealing TKMesh)

# STEP: las bibliotecas de intercambio se llaman TKDESTEP (OCCT 7.8 y 8) o
# TKSTEP (anteriores).
if(TARGET TKDESTEP)
    target_link_libraries(nuf_geometry PRIVATE TKDESTEP TKXSBase TKDE)
else()
    target_link_libraries(nuf_geometry PRIVATE TKSTEP TKXSBase)
endif()

# BREP binario (BinTools) para guardar los sólidos importados.
if(TARGET TKBinL)
    target_link_libraries(nuf_geometry PRIVATE TKBinL)
endif()

if(WIN32)
    target_compile_definitions(nuf_geometry PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
