# io: archivo nativo .nuf (SQLite) y, más adelante, STEP/STL/OBJ.
nuf_add_library(io SOURCES src/NufFile.cpp)

# CMake 4.3+ renombró el target a SQLite3::SQLite3 y marca el viejo como
# obsoleto; las versiones anteriores solo traen SQLite::SQLite3.
if(TARGET SQLite3::SQLite3)
    target_link_libraries(nuf_io PRIVATE SQLite3::SQLite3)
else()
    target_link_libraries(nuf_io PRIVATE SQLite::SQLite3)
endif()
