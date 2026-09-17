# The previewer's authoring half: the sidecar model, the session over a
# document, the document library and the diagnostics collation. No ImGui, no
# Vulkan, no window -- what a headless test drives, and what Kyusu constructs
# when these panels move into it.
file(GLOB_RECURSE SHOJI_AUTHORING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/authoring/*.cpp"
)
file(GLOB_RECURSE SHOJI_AUTHORING_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/authoring/*.h"
)
add_library(shoji_authoring STATIC
    ${SHOJI_AUTHORING_SOURCES}
    ${SHOJI_AUTHORING_HEADERS}
)
target_link_libraries(shoji_authoring PUBLIC editor_common)
sencha_warnings(shoji_authoring)
target_include_directories(shoji_authoring PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The shell: the application adapter, the composition root, and the panels.
# Disposable by design -- when these panels move into Kyusu, this target goes.
file(GLOB SHOJI_SHELL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
)
file(GLOB SHOJI_SHELL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
)
add_executable(shoji
    ${SHOJI_SHELL_SOURCES}
    ${SHOJI_SHELL_HEADERS}
)
target_link_libraries(shoji PRIVATE shoji_authoring)
sencha_warnings(shoji)
target_compile_definitions(shoji PRIVATE
    SENCHA_EDITOR_SHOJI_BRAND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/branding")
set_target_properties(shoji PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS shoji RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
