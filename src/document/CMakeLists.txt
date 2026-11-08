# document: modelo del documento (historial de operaciones, recálculo).
nuf_add_library(document SOURCES src/Document.cpp src/Expression.cpp src/Features.cpp src/FeaturesMore.cpp src/Generators.cpp)
target_link_libraries(nuf_document PUBLIC nuf::geometry nuf::sketch)
