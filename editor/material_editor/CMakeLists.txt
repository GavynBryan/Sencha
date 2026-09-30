file(GLOB_RECURSE MATERIAL_EDITOR_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

file(GLOB_RECURSE MATERIAL_EDITOR_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

# Preview shader pipeline: each entry is source|stage|embedded-variable. The
# generated headers land in ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/.
# Mesh/material rendering itself goes through the engine's MeshForwardPass;
# these cover only the editor-side backdrop.
set(MATERIAL_EDITOR_SHADERS
    "preview_backdrop.vert.glsl|vert|kPreviewBackdropVertSpv"
    "preview_backdrop.frag.glsl|frag|kPreviewBackdropFragSpv"
)

set(MATERIAL_EDITOR_GENERATED_HEADERS "")
foreach(_entry IN LISTS MATERIAL_EDITOR_SHADERS)
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
    list(APPEND MATERIAL_EDITOR_GENERATED_HEADERS "${_header}")
endforeach()

# The materials workspace as a library, so the application that hosts it links
# it; the executable is only the entry point.
list(REMOVE_ITEM MATERIAL_EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
add_library(material_editor STATIC
    ${MATERIAL_EDITOR_SOURCES}
    ${MATERIAL_EDITOR_HEADERS}
    ${MATERIAL_EDITOR_GENERATED_HEADERS}
)

target_link_libraries(material_editor PUBLIC editor_common)
sencha_warnings(material_editor)
target_include_directories(material_editor
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

add_executable(shudei src/main.cpp)
target_link_libraries(shudei PRIVATE material_editor)
sencha_warnings(shudei)

set_target_properties(shudei PROPERTIES
    INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
install(TARGETS shudei RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
