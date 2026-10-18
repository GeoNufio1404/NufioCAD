# commands: todo cambio al documento pasa por aquí (deshacer/rehacer).
nuf_add_library(commands SOURCES src/CommandStack.cpp src/RenameDocument.cpp)
target_link_libraries(nuf_commands PUBLIC nuf::document)
