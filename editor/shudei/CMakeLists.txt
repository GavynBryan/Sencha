file(GLOB_RECURSE SHUDEI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE SHUDEI_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Preview shader pipeline: each entry is source|stage|embedded-variable. The
# generated headers land in ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/.
# Mesh/material rendering itself goes through the engine's MeshForwardPass;
# these cover only the editor-side backdrop.
set(SHUDEI_SHADERS
    "preview_backdrop.vert.glsl|vert|kPreviewBackdropVertSpv"
    "preview_backdrop.frag.glsl|frag|kPreviewBackdropFragSpv"
)

set(SHUDEI_GENERATED_HEADERS "")
foreach(_entry IN LISTS SHUDEI_SHADERS)
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
    list(APPEND SHUDEI_GENERATED_HEADERS "${_header}")
endforeach()

add_executable(shudei
    ${SHUDEI_SOURCES}
    ${SHUDEI_HEADERS}
    ${SHUDEI_GENERATED_HEADERS}
)

target_link_libraries(shudei PRIVATE editor_common)
target_include_directories(shudei PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

set_target_properties(shudei PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS shudei RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
