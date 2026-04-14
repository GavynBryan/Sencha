# SenchaShaders.cmake
#
# Provides three CMake functions for the offline shader build pipeline:
#
#   sencha_compile_shader  -- compiles GLSL source to SPIR-V via glslc
#   sencha_embed_spirv     -- bakes a .spv binary into a C++ uint32_t[] header
#   sencha_embed_text      -- bakes any text file into a C++ constexpr string header
#
# Typical usage for an engine-internal (tier-1 bootstrap) shader:
#
#   sencha_compile_shader(
#       SOURCE    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.vert.glsl"
#       STAGE     vert
#       INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/include"
#       OUTPUT_SPV _sprite_vert_spv
#   )
#   sencha_embed_spirv(
#       SOURCE_SPV "${_sprite_vert_spv}"
#       VAR_NAME   kSpriteVertSpv
#       OUT_HEADER _sprite_vert_h
#   )
#   sencha_embed_text(
#       SOURCE     "${CMAKE_CURRENT_SOURCE_DIR}/shaders/internal/sprite.shader"
#       VAR_NAME   kSpriteShaderMetadata
#       OUT_HEADER _sprite_meta_h
#   )
#
# The generated headers land in ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/
# and are included as <shaders/VarName.h> once sencha_engine has that directory
# on its private include path.
#
# Dependency tracking:
#   glslc is invoked with -MD -MF to write a Makefile depfile listing every
#   #included .glsli file.  CMake's DEPFILE directive consumes this so that
#   touching any included file causes only the affected shaders to rebuild --
#   not the entire engine.

include_guard(GLOBAL)

# ── locate glslc ─────────────────────────────────────────────────────────────
# find_package(Vulkan) in CMake 3.19+ populates Vulkan_GLSLC_EXECUTABLE.
# Add a find_program fallback for non-standard SDK layouts.
if(NOT Vulkan_GLSLC_EXECUTABLE)
    find_program(Vulkan_GLSLC_EXECUTABLE
        NAMES glslc
        HINTS
            "$ENV{VULKAN_SDK}/bin"
            "${Vulkan_INCLUDE_DIRS}/../bin"
        DOC "glslc shader compiler from the Vulkan SDK"
    )
endif()

if(NOT Vulkan_GLSLC_EXECUTABLE)
    message(FATAL_ERROR
        "glslc not found.  Install the Vulkan SDK and make sure glslc is on "
        "your PATH, or set Vulkan_GLSLC_EXECUTABLE manually.\n"
        "  Download: https://vulkan.lunarg.com/sdk/home")
endif()

# ── sencha_compile_shader ─────────────────────────────────────────────────────
#
# Compiles a GLSL source file to SPIR-V using glslc.
#
# Named arguments:
#   SOURCE       (required) -- absolute path to the .glsl source file
#   STAGE        (required) -- shader stage: vert | frag | comp | geom | tesc | tese
#   OUTPUT_SPV   (required) -- name of the variable that receives the output .spv path
#   INCLUDE_DIRS (optional) -- list of directories passed as -I flags
#   DEFINES      (optional) -- preprocessor defines passed as -D flags
#   NO_OPTIMIZE  (optional) -- disable -O (useful for debugging; off by default)
function(sencha_compile_shader)
    cmake_parse_arguments(_A
        "NO_OPTIMIZE"
        "SOURCE;STAGE;OUTPUT_SPV"
        "INCLUDE_DIRS;DEFINES"
        ${ARGN}
    )

    if(NOT _A_SOURCE)
        message(FATAL_ERROR "sencha_compile_shader: SOURCE is required")
    endif()
    if(NOT _A_STAGE)
        message(FATAL_ERROR "sencha_compile_shader: STAGE is required")
    endif()
    if(NOT _A_OUTPUT_SPV)
        message(FATAL_ERROR "sencha_compile_shader: OUTPUT_SPV is required")
    endif()

    cmake_path(GET _A_SOURCE FILENAME _fname)
    set(_spv_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(_spv     "${_spv_dir}/${_fname}.spv")
    set(_dep     "${_spv_dir}/${_fname}.d")

    set(_flags "")
    foreach(_dir IN LISTS _A_INCLUDE_DIRS)
        list(APPEND _flags "-I${_dir}")
    endforeach()
    foreach(_def IN LISTS _A_DEFINES)
        list(APPEND _flags "-D${_def}")
    endforeach()
    if(NOT _A_NO_OPTIMIZE)
        list(APPEND _flags "-O")
    endif()

    add_custom_command(
        OUTPUT  "${_spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_spv_dir}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}"
                "-fshader-stage=${_A_STAGE}"
                "--target-env=vulkan1.3"
                "-MD" "-MF" "${_dep}"
                ${_flags}
                "${_A_SOURCE}" "-o" "${_spv}"
        DEPENDS "${_A_SOURCE}"
        DEPFILE "${_dep}"
        COMMENT "Compiling shader ${_fname}"
        VERBATIM
    )

    set(${_A_OUTPUT_SPV} "${_spv}" PARENT_SCOPE)
endfunction()

# ── sencha_embed_spirv ────────────────────────────────────────────────────────
#
# Reads a compiled .spv binary and generates a C++ header containing the SPIR-V
# words as a constexpr uint32_t array.  The array can be passed directly to
# VulkanShaderCache::CreateModuleFromSpirv without any file I/O.
#
# Named arguments:
#   SOURCE_SPV  (required) -- absolute path to the .spv file
#   VAR_NAME    (required) -- C++ identifier for the generated array
#   OUT_HEADER  (required) -- name of the variable that receives the header path
function(sencha_embed_spirv)
    cmake_parse_arguments(_A "" "SOURCE_SPV;VAR_NAME;OUT_HEADER" "" ${ARGN})

    if(NOT _A_SOURCE_SPV OR NOT _A_VAR_NAME OR NOT _A_OUT_HEADER)
        message(FATAL_ERROR
            "sencha_embed_spirv: SOURCE_SPV, VAR_NAME, and OUT_HEADER are required")
    endif()

    set(_header "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${_A_VAR_NAME}.h")

    add_custom_command(
        OUTPUT  "${_header}"
        COMMAND "${CMAKE_COMMAND}"
                "-DINPUT_SPV=${_A_SOURCE_SPV}"
                "-DVAR_NAME=${_A_VAR_NAME}"
                "-DOUTPUT_HEADER=${_header}"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        DEPENDS "${_A_SOURCE_SPV}"
                "${CMAKE_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
        COMMENT "Embedding SPIR-V ${_A_VAR_NAME}"
        VERBATIM
    )

    set(${_A_OUT_HEADER} "${_header}" PARENT_SCOPE)
endfunction()

# ── sencha_embed_text ─────────────────────────────────────────────────────────
#
# Reads any text file and generates a C++ header containing its contents as a
# constexpr null-terminated string.  Use for pipeline metadata (.shader files),
# small configs, etc.  Not for GLSL source intended for runtime compilation.
#
# Named arguments:
#   SOURCE     (required) -- absolute path to the text file
#   VAR_NAME   (required) -- C++ identifier for the generated string constant
#   OUT_HEADER (required) -- name of the variable that receives the header path
function(sencha_embed_text)
    cmake_parse_arguments(_A "" "SOURCE;VAR_NAME;OUT_HEADER" "" ${ARGN})

    if(NOT _A_SOURCE OR NOT _A_VAR_NAME OR NOT _A_OUT_HEADER)
        message(FATAL_ERROR
            "sencha_embed_text: SOURCE, VAR_NAME, and OUT_HEADER are required")
    endif()

    set(_header "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders/${_A_VAR_NAME}.h")

    add_custom_command(
        OUTPUT  "${_header}"
        COMMAND "${CMAKE_COMMAND}"
                "-DINPUT_FILE=${_A_SOURCE}"
                "-DVAR_NAME=${_A_VAR_NAME}"
                "-DOUTPUT_HEADER=${_header}"
                "-P" "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
        DEPENDS "${_A_SOURCE}"
                "${CMAKE_SOURCE_DIR}/cmake/EmbedText.cmake"
        COMMENT "Embedding text ${_A_VAR_NAME}"
        VERBATIM
    )

    set(${_A_OUT_HEADER} "${_header}" PARENT_SCOPE)
endfunction()
