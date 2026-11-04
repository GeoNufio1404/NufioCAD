# io: archivo nativo .nuf (SQLite, parámetros en JSON) y, más adelante, importar STEP.
nuf_add_library(io SOURCES src/NufFile.cpp src/DocumentFile.cpp)
target_link_libraries(nuf_io PUBLIC nuf::document)
target_link_libraries(nuf_io PRIVATE nlohmann_json::nlohmann_json)

# CMake 4.3+ renombró el target a SQLite3::SQLite3 y marca el viejo como
# obsoleto; las versiones anteriores solo traen SQLite::SQLite3.
if(TARGET SQLite3::SQLite3)
    target_link_libraries(nuf_io PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(nuf_io PRIVATE SQLite::SQLite3)
endif()
