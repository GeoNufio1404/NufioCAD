# geometry: único módulo que habla directamente con OpenCASCADE.
nuf_add_library(geometry SOURCES src/Kernel.cpp)

target_include_directories(nuf_geometry SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(nuf_geometry PRIVATE
    TKernel TKMath TKG3d TKGeomBase TKBRep TKPrim TKTopAlgo)

if(WIN32)
    target_compile_definitions(nuf_geometry PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
