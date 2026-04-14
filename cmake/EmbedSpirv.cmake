# cmake -P EmbedSpirv.cmake
#
# Reads a compiled SPIR-V binary and writes a C++ header that exposes the words
# as a constexpr uint32_t array.  Engine code includes this header and passes
# the array pointer directly to vkCreateShaderModule -- zero file I/O, zero
# runtime compiler dependency.
#
# Required variables (pass via -D on the cmake command line):
#   INPUT_SPV      -- path to the compiled .spv file
#   VAR_NAME       -- C++ identifier for the generated array
#   OUTPUT_HEADER  -- path of the header to write

if(NOT INPUT_SPV OR NOT VAR_NAME OR NOT OUTPUT_HEADER)
    message(FATAL_ERROR
        "EmbedSpirv: missing required variable(s).\n"
        "  INPUT_SPV     = '${INPUT_SPV}'\n"
        "  VAR_NAME      = '${VAR_NAME}'\n"
        "  OUTPUT_HEADER = '${OUTPUT_HEADER}'")
endif()

if(NOT EXISTS "${INPUT_SPV}")
    message(FATAL_ERROR "EmbedSpirv: file not found: ${INPUT_SPV}")
endif()

# Read raw bytes as a lowercase hex string (no separators).
file(READ "${INPUT_SPV}" _raw HEX)
string(LENGTH "${_raw}" _hexlen)

if(_hexlen EQUAL 0)
    message(FATAL_ERROR "EmbedSpirv: empty SPIR-V file: ${INPUT_SPV}")
endif()

# SPIR-V binary must be a multiple of 4 bytes (one word each).
math(EXPR _rem "${_hexlen} % 8")
if(NOT _rem EQUAL 0)
    message(FATAL_ERROR
        "EmbedSpirv: file size is not a multiple of 4 bytes: ${INPUT_SPV}")
endif()

cmake_path(GET INPUT_SPV FILENAME _fname)
math(EXPR _wordcount "${_hexlen} / 8")
math(EXPR _bytecount "${_hexlen} / 2")

# Build a comma-separated list of uint32_t hex literals.
#
# glslc writes SPIR-V in the native byte order of the host, which on every
# platform Sencha targets (x86-64, ARM64) is little-endian.  The hex string
# therefore presents bytes in little-endian file order; to form a correctly
# valued C++ uint32_t literal we read four bytes and write the most-significant
# byte first (big-endian literal notation):
#
#   file bytes: B0 B1 B2 B3   (B0 at lowest address = least significant)
#   C++ literal: 0xB3B2B1B0
#
# NOTE: For shaders larger than ~100 KB (~25 K words) this CMake script loop
# may take several seconds.  If that becomes a bottleneck, replace this script
# with a small compiled tool and keep the add_custom_command wrapper unchanged.
set(_words "")
set(_i 0)
while(_i LESS _hexlen)
    string(SUBSTRING "${_raw}" ${_i} 2 _b0)    # least-significant byte
    math(EXPR _i1 "${_i} + 2")
    math(EXPR _i2 "${_i} + 4")
    math(EXPR _i3 "${_i} + 6")
    string(SUBSTRING "${_raw}" ${_i1} 2 _b1)
    string(SUBSTRING "${_raw}" ${_i2} 2 _b2)
    string(SUBSTRING "${_raw}" ${_i3} 2 _b3)   # most-significant byte
    list(APPEND _words "0x${_b3}${_b2}${_b1}${_b0}")
    math(EXPR _i "${_i} + 8")
endwhile()

list(JOIN _words ", " _word_list)

# Ensure the output directory exists.
cmake_path(GET OUTPUT_HEADER PARENT_PATH _out_dir)
file(MAKE_DIRECTORY "${_out_dir}")

file(WRITE "${OUTPUT_HEADER}"
    "// Auto-generated from ${_fname} (${_wordcount} words / ${_bytecount} bytes).\n"
    "// Do not edit -- regenerated every time the shader is compiled.\n"
    "// NOLINTBEGIN(*-avoid-c-arrays, *-magic-numbers)\n"
    "#pragma once\n"
    "\n"
    "#include <cstdint>\n"
    "\n"
    "// clang-format off\n"
    "inline constexpr uint32_t ${VAR_NAME}[] = { ${_word_list} };\n"
    "inline constexpr uint32_t ${VAR_NAME}WordCount =\n"
    "    static_cast<uint32_t>(sizeof(${VAR_NAME}) / sizeof(${VAR_NAME}[0]));\n"
    "// clang-format on\n"
    "// NOLINTEND(*-avoid-c-arrays, *-magic-numbers)\n"
)
