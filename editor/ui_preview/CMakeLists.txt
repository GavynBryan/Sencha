# The previewer's authoring half: the sidecar model, the session over a
# document, the document library and the diagnostics collation. No ImGui, no
# Vulkan, no window -- what a headless test drives, and what Kyusu constructs
# when these panels move into it.
file(GLOB_RECURSE UI_PREVIEW_AUTHORING_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/authoring/*.cpp"
)
file(GLOB_RECURSE UI_PREVIEW_AUTHORING_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/authoring/*.h"
)
add_library(ui_preview_authoring STATIC
    ${UI_PREVIEW_AUTHORING_SOURCES}
    ${UI_PREVIEW_AUTHORING_HEADERS}
)
target_link_libraries(ui_preview_authoring PUBLIC editor_common)
sencha_warnings(ui_preview_authoring)
target_include_directories(ui_preview_authoring PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The UI preview workspace's shell: the composition root and the panels. A
# library so the application that hosts it links it; the executable is only
# the entry point.
file(GLOB UI_PREVIEW_SHELL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
)
list(REMOVE_ITEM UI_PREVIEW_SHELL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
file(GLOB UI_PREVIEW_SHELL_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h"
)
add_library(ui_preview STATIC
    ${UI_PREVIEW_SHELL_SOURCES}
    ${UI_PREVIEW_SHELL_HEADERS}
)
target_link_libraries(ui_preview PUBLIC ui_preview_authoring)
sencha_warnings(ui_preview)
target_compile_definitions(ui_preview PRIVATE
    SENCHA_EDITOR_SHOJI_BRAND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/branding")

add_executable(shoji src/main.cpp)
target_link_libraries(shoji PRIVATE ui_preview)
sencha_warnings(shoji)
set_target_properties(shoji PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS shoji RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
