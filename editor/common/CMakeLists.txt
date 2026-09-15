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
sencha_warnings(editor_common)

# SVG parsing and rasterization for the editor icons (editor/icons), read once
# at startup into the ImGui font atlas. Header-only; SOURCE_SUBDIR names a
# directory the repo lacks so FetchContent downloads without building its own
# project (the engine's form for cgltf and mikktspace). Editor-only: never
# reaches a game binary. The implementation TU is ui/chrome/IconDraw.cpp.
include(FetchContent)
FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6
    SOURCE_SUBDIR  sencha-does-not-build-this
)
FetchContent_MakeAvailable(nanosvg)
target_include_directories(editor_common SYSTEM PRIVATE "${nanosvg_SOURCE_DIR}/src")

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
    # The faces live under the authored-UI content root, because a cooked UI
    # document declares the ones it uses and the cooker resolves them inside
    # that root. One copy, two consumers: ImGui loads them from here directly,
    # and an authored document gets them as cooked .sfont assets. The Font
    # Awesome *header* stays at editor/fonts/, where the "fonts/..." include
    # expects it.
    SENCHA_EDITOR_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../ui/fonts"
    SENCHA_EDITOR_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../ui"
    SENCHA_EDITOR_THEME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../themes"
    SENCHA_EDITOR_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../icons"
)
