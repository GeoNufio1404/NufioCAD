# commands: todo cambio al documento pasa por aquí (deshacer/rehacer).
nuf_add_library(commands SOURCES src/CommandStack.cpp src/FeatureCommands.cpp src/RenameDocument.cpp src/SetParameters.cpp)
target_link_libraries(nuf_commands PUBLIC nuf::document)
