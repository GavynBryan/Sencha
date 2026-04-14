# cmake -P EmbedText.cmake
#
# Reads any text file and writes a C++ header that exposes its contents as a
# constexpr null-terminated string.  Intended for small data files that need
# to be available before the file system / asset system is initialised:
# shader pipeline metadata (.shader), default configs, etc.
#
# Do NOT use this for GLSL source code that will be fed to a runtime compiler.
# That code path is reserved for SENCHA_ENABLE_HOT_RELOAD development builds;
# shipping builds must compile shaders offline (see EmbedSpirv.cmake).
#
# Required variables (pass via -D on the cmake command line):
#   INPUT_FILE     -- path to the source text file
#   VAR_NAME       -- C++ identifier for the generated string constant
#   OUTPUT_HEADER  -- path of the header to write

if(NOT INPUT_FILE OR NOT VAR_NAME OR NOT OUTPUT_HEADER)
    message(FATAL_ERROR
        "EmbedText: missing required variable(s).\n"
        "  INPUT_FILE    = '${INPUT_FILE}'\n"
        "  VAR_NAME      = '${VAR_NAME}'\n"
        "  OUTPUT_HEADER = '${OUTPUT_HEADER}'")
endif()

if(NOT EXISTS "${INPUT_FILE}")
    message(FATAL_ERROR "EmbedText: file not found: ${INPUT_FILE}")
endif()

file(READ "${INPUT_FILE}" _content)
cmake_path(GET INPUT_FILE FILENAME _fname)

# Ensure the output directory exists.
cmake_path(GET OUTPUT_HEADER PARENT_PATH _out_dir)
file(MAKE_DIRECTORY "${_out_dir}")

# Use a raw string literal with a delimiter that cannot appear in JSON or
# typical configuration files.
file(WRITE "${OUTPUT_HEADER}"
    "// Auto-generated from ${_fname} -- do not edit.\n"
    "#pragma once\n"
    "\n"
    "inline constexpr const char* ${VAR_NAME} = R\"SENCHA_EMBED(\n"
    "${_content}"
    ")SENCHA_EMBED\";\n"
)
