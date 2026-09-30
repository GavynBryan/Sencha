file(GLOB_RECURSE DATA_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE DATA_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The data workspace, linked by the application that hosts it.
add_library(data_editor STATIC
    ${DATA_EDITOR_SOURCES}
    ${DATA_EDITOR_HEADERS}
)

target_link_libraries(data_editor PUBLIC editor_common)
sencha_warnings(data_editor)
target_include_directories(data_editor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
