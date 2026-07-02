file(GLOB_RECURSE CHAKIN_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE CHAKIN_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# The preview draws through the engine's MeshForwardPass; no editor shaders.
add_executable(chakin
    ${CHAKIN_SOURCES}
    ${CHAKIN_HEADERS}
)

target_link_libraries(chakin PRIVATE editor_common)
target_include_directories(chakin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(chakin PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS chakin RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
