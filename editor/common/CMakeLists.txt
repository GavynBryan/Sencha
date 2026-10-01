file(GLOB_RECURSE EDITOR_COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE EDITOR_COMMON_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Shaders for the overlay pipelines every editor workspace draws with: each
# entry is source|stage|embedded-variable, generated into
# ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/.
set(EDITOR_COMMON_SHADERS
    "editor_wideline.vert.glsl|vert|kEditorWideLineVertSpv"
    "editor_wideline.frag.glsl|frag|kEditorWideLineFragSpv"
)
set(EDITOR_COMMON_GENERATED_HEADERS "")
foreach(_entry IN LISTS EDITOR_COMMON_SHADERS)
    string(REPLACE "|" ";" _entry "${_entry}")
    list(GET _entry 0 _file)
    list(GET _entry 1 _stage)
    list(GET _entry 2 _var)
    sencha_compile_shader(
        SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_file}"
        STAGE      "${_stage}"
        OUTPUT_SPV _spv
    )
    sencha_embed_spirv(
        SOURCE_SPV "${_spv}"
        VAR_NAME   "${_var}"
        OUT_HEADER _header
    )
    list(APPEND EDITOR_COMMON_GENERATED_HEADERS "${_header}")
endforeach()

add_library(editor_common STATIC
    ${EDITOR_COMMON_SOURCES}
    ${EDITOR_COMMON_HEADERS}
    ${EDITOR_COMMON_GENERATED_HEADERS}
)

target_link_libraries(editor_common PUBLIC sencha::engine)
sencha_warnings(editor_common)


# The parent editor/ dir carries the shared fonts/ headers ("fonts/...").
# Application-private sources are NOT on this path: an editor application can
# see common headers, but common can never resolve an application's.
target_include_directories(editor_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_include_directories(editor_common PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

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
    SENCHA_EDITOR_ICON_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../ui/icons"
)
