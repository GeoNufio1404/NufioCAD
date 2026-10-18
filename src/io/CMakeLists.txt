# io: archivo nativo .nuf (SQLite) y, más adelante, STEP/STL/OBJ.
nuf_add_library(io SOURCES src/NufFile.cpp)
target_link_libraries(nuf_io PRIVATE SQLite::SQLite3)
