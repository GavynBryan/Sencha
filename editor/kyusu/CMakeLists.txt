file(GLOB_RECURSE KYUSU_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE KYUSU_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Viewport shader pipeline: each entry is source|stage|embedded-variable. The
# generated headers land in ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/.
set(KYUSU_SHADERS
    "grid.vert.glsl|vert|kGridVertSpv"
    "grid.frag.glsl|frag|kGridFragSpv"
    "editor_line.vert.glsl|vert|kEditorLineVertSpv"
    "editor_line.frag.glsl|frag|kEditorLineFragSpv"
    "editor_wideline.vert.glsl|vert|kEditorWideLineVertSpv"
    "editor_wideline.frag.glsl|frag|kEditorWideLineFragSpv"
    "fullscreen.vert.glsl|vert|kFullscreenVertSpv"
    "bloom_brightpass.frag.glsl|frag|kBloomBrightpassFragSpv"
    "bloom_blur.frag.glsl|frag|kBloomBlurFragSpv"
    "bloom_composite.frag.glsl|frag|kBloomCompositeFragSpv"
    "viewport_backdrop.vert.glsl|vert|kViewportBackdropVertSpv"
    "viewport_backdrop.frag.glsl|frag|kViewportBackdropFragSpv"
    "editor_solid.vert.glsl|vert|kEditorSolidVertSpv"
    "editor_solid.frag.glsl|frag|kEditorSolidFragSpv"
)

set(KYUSU_GENERATED_HEADERS "")
foreach(_entry IN LISTS KYUSU_SHADERS)
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
    list(APPEND KYUSU_GENERATED_HEADERS "${_header}")
endforeach()

add_executable(kyusu
    ${KYUSU_SOURCES}
    ${KYUSU_HEADERS}
    ${KYUSU_GENERATED_HEADERS}
)

target_link_libraries(kyusu PRIVATE editor_common)
target_include_directories(kyusu PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

target_compile_definitions(kyusu PRIVATE
    # Dev-time editor visual assets (e.g. camera.glb). Packaging will replace
    # this with a resolved install path.
    SENCHA_EDITOR_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/meshes"
)

# Install into the SDK (real-engine-roadmap.md Phase 0). RPATH matches the
# app's bin/ + lib/ layout.
set_target_properties(kyusu PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS kyusu RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
