file(GLOB_RECURSE EDITOR_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE EDITOR_COMMON_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(editor_common STATIC
    ${EDITOR_COMMON_SOURCES}
    ${EDITOR_COMMON_HEADERS}
)

target_link_libraries(editor_common PUBLIC sencha::engine)

# The parent editor/ dir carries the shared fonts/ headers ("fonts/...").
# Application-private sources are NOT on this path: an editor application can
# see common headers, but common can never resolve an application's.
target_include_directories(editor_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_compile_definitions(editor_common PUBLIC
    SENCHA_ENABLE_VULKAN
    SENCHA_ENABLE_DEBUG_UI
    # Dev-time UI asset roots shared by every editor application. Packaging will
    # replace these with resolved install paths.
    SENCHA_EDITOR_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fonts"
    SENCHA_EDITOR_SKIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../skin"
    SENCHA_EDITOR_THEME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../themes"
)
