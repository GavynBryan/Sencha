# The previewer's authoring half: the sidecar model, the session over a
# document, the document library, the cards and explorers over them and the
# diagnostics collation. No toolkit, no Vulkan, no window -- what a headless
# test drives.
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

# The UI preview workspace's composition root, linked by the application that
# hosts it.
add_library(ui_preview STATIC
    src/UiPreviewWorkspace.cpp
    src/UiPreviewWorkspace.h
)
target_link_libraries(ui_preview PUBLIC ui_preview_authoring)
sencha_warnings(ui_preview)
