# sencha_generate_component_metadata(<target> HEADERS <header>...)
#
# Generates a ComponentDefinition companion beside each annotated component
# header, and an index sidecar the validation stage reads.
#
# The same function serves the engine build and an out-of-tree game module, so a
# game declares components exactly the way the engine does. The flags come from
# the consuming target's own properties rather than from a compile database:
# a header has no entry in compile_commands.json, and an imported target has no
# compile commands at all.
#
# Only -D/-I/-isystem/-std are passed, so a GCC-built engine is safe to parse
# with Clang -- the preprocessor environment is reproduced exactly while
# toolchain-specific flags are excluded by construction.

set(SENCHA_COMPONENT_CODEGEN_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "")

function(sencha_generate_component_metadata target)
    cmake_parse_arguments(ARG "" "" "HEADERS" ${ARGN})
    if(NOT ARG_HEADERS)
        return()
    endif()

    if(NOT TARGET sencha-component-codegen AND NOT TARGET sencha::component-codegen)
        message(FATAL_ERROR
            "sencha_generate_component_metadata(${target}): annotated component headers "
            "were given but sencha-component-codegen is not available. Install the Clang "
            "development packages (clang-devel/llvm-devel) and reconfigure.")
    endif()
    if(TARGET sencha::component-codegen)
        set(_tool sencha::component-codegen)
    else()
        set(_tool sencha-component-codegen)
    endif()

    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated-components")
    set(_flags "${CMAKE_CURRENT_BINARY_DIR}/component-codegen.rsp")

    # Written from the target's own properties, the same source of truth CMake
    # expands into the real compile line.
    file(GENERATE OUTPUT "${_flags}" CONTENT
"-std=c++20
-DSENCHA_CODEGEN=1
-Wno-pragma-once-outside-header
$<JOIN:$<LIST:TRANSFORM,$<REMOVE_DUPLICATES:$<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>>,PREPEND,-D>,
>
$<JOIN:$<LIST:TRANSFORM,$<REMOVE_DUPLICATES:$<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>>,PREPEND,-I>,
>
")

    set(_outputs "")
    set(_indexes "")
    foreach(_header IN LISTS ARG_HEADERS)
        get_filename_component(_abs "${_header}" ABSOLUTE)
        file(RELATIVE_PATH _logical "${CMAKE_CURRENT_SOURCE_DIR}" "${_abs}")
        get_filename_component(_dir "${_logical}" DIRECTORY)
        get_filename_component(_stem "${_logical}" NAME_WE)

        set(_companion "${_generated}/${_dir}/${_stem}.sencha.h")
        set(_index "${_generated}/${_dir}/${_stem}.index")

        # A placeholder at configure time so the tree is always includable: a
        # fresh checkout compiles, and an editor does not show red before the
        # first build. The build-time step overwrites it.
        if(NOT EXISTS "${_companion}")
            file(WRITE "${_companion}" "#pragma once\n// Placeholder; regenerated at build time.\n")
        endif()

        add_custom_command(
            OUTPUT "${_companion}" "${_index}"
            COMMAND ${_tool} "${_abs}"
                    "--output=${_companion}" "--index=${_index}" "--logical=${_logical}"
                    -- "@${_flags}"
            DEPENDS "${_abs}" "${_flags}" ${_tool}
            COMMENT "Generating component metadata for ${_logical}"
            VERBATIM)

        list(APPEND _outputs "${_companion}")
        list(APPEND _indexes "${_index}")
    endforeach()

    target_sources(${target} PRIVATE ${_outputs})
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${_generated}>")

    # Per-header generation cannot see a collision with another header, so one
    # aggregate stage reads every index. BASE_INDEX lets an out-of-tree module
    # be checked against engine components it cannot see.
    set(_validated "${CMAKE_CURRENT_BINARY_DIR}/${target}.components-validated")
    set(_base_index "")
    if(DEFINED SENCHA_BASE_COMPONENT_INDEX)
        set(_base_index "-DBASE_INDEX=${SENCHA_BASE_COMPONENT_INDEX}")
    endif()

    add_custom_command(
        OUTPUT "${_validated}"
        COMMAND ${CMAKE_COMMAND} -DINDEX_DIR=${_generated} ${_base_index}
                -P "${SENCHA_COMPONENT_CODEGEN_DIR}/ValidateComponentIndex.cmake"
        COMMAND ${CMAKE_COMMAND} -E touch "${_validated}"
        DEPENDS ${_indexes} "${SENCHA_COMPONENT_CODEGEN_DIR}/ValidateComponentIndex.cmake"
        COMMENT "Validating component metadata for ${target}"
        VERBATIM)

    add_custom_target(${target}_component_index DEPENDS "${_validated}")
    add_dependencies(${target} ${target}_component_index)
endfunction()
