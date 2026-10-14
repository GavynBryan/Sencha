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


# Application-private sources are NOT on this path: an editor application can
# see common headers, but common can never resolve an application's.
target_include_directories(editor_common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(editor_common PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(editor_common PUBLIC
    SENCHA_ENABLE_VULKAN
    SENCHA_ENABLE_DEBUG_UI
    # Dev-time UI asset roots shared by every editor application. Packaging will
    # replace these with resolved install paths.
    SENCHA_EDITOR_UI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../ui"
    SENCHA_EDITOR_THEME_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../themes"
)
