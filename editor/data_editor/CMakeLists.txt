file(GLOB_RECURSE DATA_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM DATA_EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

file(GLOB_RECURSE DATA_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The data workspace as a library, so the application that hosts it links it;
# the executable is only the entry point.
add_library(data_editor STATIC
    ${DATA_EDITOR_SOURCES}
    ${DATA_EDITOR_HEADERS}
)

target_link_libraries(data_editor PUBLIC editor_common)
sencha_warnings(data_editor)
target_include_directories(data_editor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

add_executable(data_editor_app src/main.cpp)
target_link_libraries(data_editor_app PRIVATE data_editor)
sencha_warnings(data_editor_app)
set_target_properties(data_editor_app PROPERTIES
    OUTPUT_NAME data_editor
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS data_editor_app RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
