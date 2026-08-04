file(GLOB_RECURSE DATA_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE DATA_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(data_editor
    ${DATA_EDITOR_SOURCES}
    ${DATA_EDITOR_HEADERS}
)

target_link_libraries(data_editor PRIVATE editor_common)
target_include_directories(data_editor PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(data_editor PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS data_editor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
