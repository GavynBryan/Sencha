# sencha_generate_component_metadata(<target> [INCLUDE_ROOT <dir>] [BASE <target>]
#                                    HEADERS <header>...)
#
# Generates a ComponentDefinition companion beside each annotated component
# header, and an index sidecar the validation stage reads. The validated
# sidecars are joined into one index, recorded on the target as its
# SENCHA_COMPONENT_INDEX property: the roster its tests check against what it
# registers, and the file the SDK installs for game modules to validate
# against.
#
# BASE names a target whose SENCHA_COMPONENT_INDEX the validation stage reads
# alongside this target's own, so a game module is checked against the engine
# components it cannot otherwise see: sencha::engine, built in-tree or imported
# from an SDK.
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

# sencha_require_component_codegen_format(<include-dir>)
#
# Refuses a prebuilt sencha::component-codegen whose companion format is not
# the one core/metadata/ComponentDefinition.h under <include-dir> reads. The
# companions' own static_assert would catch the mismatch, but only after the
# whole build has been configured and the first component compiled.
function(sencha_require_component_codegen_format include_dir)
    set(_header "${include_dir}/core/metadata/ComponentDefinition.h")
    file(STRINGS "${_header}" _declaration REGEX "kComponentCodegenFormatVersion = [0-9]+")
    if(NOT _declaration MATCHES "kComponentCodegenFormatVersion = ([0-9]+)")
        message(FATAL_ERROR "${_header} does not declare kComponentCodegenFormatVersion")
    endif()
    set(_expected "${CMAKE_MATCH_1}")

    get_target_property(_generator sencha::component-codegen IMPORTED_LOCATION)
    execute_process(
        COMMAND "${_generator}" --format-version
        OUTPUT_VARIABLE _actual
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "${_generator} could not report its component metadata format (--format-version)")
    endif()
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR
            "${_generator} writes component metadata format ${_actual}, but ${_header} "
            "reads format ${_expected}. Build the generator from the same revision as "
            "the headers.")
    endif()
endfunction()

function(sencha_generate_component_metadata target)
    cmake_parse_arguments(ARG "" "INCLUDE_ROOT;BASE" "HEADERS" ${ARGN})
    if(NOT ARG_INCLUDE_ROOT)
        set(ARG_INCLUDE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    get_filename_component(ARG_INCLUDE_ROOT "${ARG_INCLUDE_ROOT}" ABSOLUTE)
    if(NOT ARG_HEADERS)
        return()
    endif()

    set(_generated "${CMAKE_CURRENT_BINARY_DIR}/generated-components")
    set(_flags "${CMAKE_CURRENT_BINARY_DIR}/component-codegen.rsp")

    # Every configuration compiles the same companion, so the flags come from
    # one reference configuration; a definition such as NDEBUG that varies with
    # the configuration cannot change what a component declares.
    set(_reference_config "")
    get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(_multi_config)
        list(GET CMAKE_CONFIGURATION_TYPES 0 _reference)
        set(_reference_config CONDITION "$<CONFIG:${_reference}>")
    endif()

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
" ${_reference_config})

    set(_outputs "")
    set(_indexes "")
    foreach(_header IN LISTS ARG_HEADERS)
        get_filename_component(_abs "${_header}" ABSOLUTE)
        # Relative to the include root, so the companion is reachable by the
        # same logical path the component header is included by.
        file(RELATIVE_PATH _logical "${ARG_INCLUDE_ROOT}" "${_abs}")
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
            COMMAND sencha::component-codegen "${_abs}"
                    "--output=${_companion}" "--index=${_index}"
                    "--logical=${_logical}" "--flags=${_flags}"
            DEPENDS "${_abs}" "${_flags}" sencha::component-codegen
            COMMENT "Generating component metadata for ${_logical}"
            VERBATIM)

        list(APPEND _outputs "${_companion}")
        list(APPEND _indexes "${_index}")
    endforeach()

    target_sources(${target} PRIVATE ${_outputs})
    target_include_directories(${target} PUBLIC "$<BUILD_INTERFACE:${_generated}>")

    # A header dropped from the list leaves its companion and sidecar behind,
    # and the validation stage reads every sidecar it finds.
    file(GLOB_RECURSE _stale "${_generated}/*.sencha.h" "${_generated}/*.index")
    list(REMOVE_ITEM _stale ${_outputs} ${_indexes})
    if(_stale)
        file(REMOVE ${_stale})
    endif()

    # Per-header generation cannot see a collision with another header, so one
    # aggregate stage reads every index.
    set(_index "${CMAKE_CURRENT_BINARY_DIR}/${target}.components.index")
    set(_base_index "")
    set(_base_argument "")
    if(ARG_BASE)
        get_target_property(_base_index ${ARG_BASE} SENCHA_COMPONENT_INDEX)
        if(NOT _base_index)
            message(FATAL_ERROR
                "sencha_generate_component_metadata(${target}): ${ARG_BASE} has no SENCHA_COMPONENT_INDEX")
        endif()
        set(_base_argument "-DBASE_INDEX=${_base_index}")
    endif()

    add_custom_command(
        OUTPUT "${_index}"
        COMMAND ${CMAKE_COMMAND} -DINDEX_DIR=${_generated} -DOUTPUT=${_index} ${_base_argument}
                -P "${SENCHA_COMPONENT_CODEGEN_DIR}/ValidateComponentIndex.cmake"
        DEPENDS ${_indexes} ${_base_index}
                "${SENCHA_COMPONENT_CODEGEN_DIR}/ValidateComponentIndex.cmake"
        COMMENT "Validating component metadata for ${target}"
        VERBATIM)

    add_custom_target(${target}_component_index DEPENDS "${_index}")
    if(ARG_BASE)
        # An in-tree base writes its index during the build; an imported one
        # already has it, and a dependency on an imported target is a no-op.
        add_dependencies(${target}_component_index ${ARG_BASE})
    endif()
    add_dependencies(${target} ${target}_component_index)
    set_property(TARGET ${target} PROPERTY SENCHA_COMPONENT_INDEX "${_index}")
endfunction()
