function(sencha_collect_include_prefixes out_var include_dir)
    file(GLOB children CONFIGURE_DEPENDS
        LIST_DIRECTORIES true
        "${include_dir}/*"
    )

    set(prefixes)
    foreach(child IN LISTS children)
        get_filename_component(name "${child}" NAME)
        if(IS_DIRECTORY "${child}")
            list(APPEND prefixes "${name}/")
        else()
            list(APPEND prefixes "${name}")
        endif()
    endforeach()

    set(${out_var} ${prefixes} PARENT_SCOPE)
endfunction()

function(sencha_assert_layer_boundary layer_name)
    set(options)
    set(one_value_args)
    set(multi_value_args FILES FORBIDDEN_INCLUDE_PREFIXES FORBIDDEN_PATH_TOKENS)
    cmake_parse_arguments(SENCHA_LAYER "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(violations)
    foreach(file_path IN LISTS SENCHA_LAYER_FILES)
        if(NOT EXISTS "${file_path}")
            continue()
        endif()

        file(RELATIVE_PATH relative_path "${CMAKE_SOURCE_DIR}" "${file_path}")
        file(STRINGS "${file_path}" include_lines REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"].*[>\"]")

        foreach(line IN LISTS include_lines)
            if(NOT line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
                continue()
            endif()

            set(include_path "${CMAKE_MATCH_1}")
            string(REPLACE "\\" "/" normalized_include_path "${include_path}")

            foreach(prefix IN LISTS SENCHA_LAYER_FORBIDDEN_INCLUDE_PREFIXES)
                if(normalized_include_path MATCHES "^${prefix}")
                    string(APPEND violations
                        "\n  ${relative_path}: #include <${include_path}> uses forbidden public prefix '${prefix}'"
                    )
                endif()
            endforeach()

            foreach(path_token IN LISTS SENCHA_LAYER_FORBIDDEN_PATH_TOKENS)
                if(normalized_include_path MATCHES "(^|/)${path_token}(/|$)")
                    string(APPEND violations
                        "\n  ${relative_path}: #include <${include_path}> reaches forbidden layer path '${path_token}'"
                    )
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(violations)
        message(FATAL_ERROR
            "Sencha layer boundary violation in ${layer_name}:${violations}\n"
            "Layer order is kettle -> teapot -> infuser. A layer may include itself and lower layers only."
        )
    endif()
endfunction()

function(_sencha_collect_target_link_closure target_name visited_targets out_var)
    if(NOT TARGET ${target_name})
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    get_target_property(aliased_target ${target_name} ALIASED_TARGET)
    if(aliased_target)
        set(target_name ${aliased_target})
    endif()

    list(FIND visited_targets ${target_name} already_visited)
    if(NOT already_visited EQUAL -1)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    list(APPEND visited_targets ${target_name})

    set(link_closure)
    foreach(property_name LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(link_entries ${target_name} ${property_name})
        if(NOT link_entries OR link_entries STREQUAL "link_entries-NOTFOUND")
            continue()
        endif()

        foreach(link_entry IN LISTS link_entries)
            if(link_entry MATCHES "^\\$<")
                continue()
            endif()

            list(APPEND link_closure ${link_entry})
            if(TARGET ${link_entry})
                get_target_property(link_entry_alias ${link_entry} ALIASED_TARGET)
                if(link_entry_alias)
                    list(APPEND link_closure ${link_entry_alias})
                    set(link_entry_target ${link_entry_alias})
                else()
                    set(link_entry_target ${link_entry})
                endif()

                _sencha_collect_target_link_closure(${link_entry_target} "${visited_targets}" nested_link_closure)
                list(APPEND link_closure ${nested_link_closure})
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES link_closure)
    set(${out_var} ${link_closure} PARENT_SCOPE)
endfunction()

function(sencha_assert_target_does_not_link target_name)
    set(options)
    set(one_value_args)
    set(multi_value_args FORBIDDEN_TARGETS)
    cmake_parse_arguments(SENCHA_TARGET "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    _sencha_collect_target_link_closure(${target_name} "" link_closure)

    set(violations)
    foreach(link_entry IN LISTS link_closure)
        foreach(forbidden_target IN LISTS SENCHA_TARGET_FORBIDDEN_TARGETS)
            if(link_entry STREQUAL forbidden_target)
                string(APPEND violations
                    "\n  ${target_name} link closure contains forbidden dependency '${forbidden_target}'"
                )
            endif()
        endforeach()
    endforeach()

    if(violations)
        message(FATAL_ERROR
            "Sencha target graph boundary violation:${violations}\n"
            "Layer order is kettle -> teapot -> infuser. Lower layers may not link higher layers or implementation backends."
        )
    endif()
endfunction()

function(sencha_assert_target_does_not_include target_name)
    set(options)
    set(one_value_args)
    set(multi_value_args FORBIDDEN_INCLUDE_DIRS)
    cmake_parse_arguments(SENCHA_TARGET "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    set(normalized_forbidden_dirs)
    foreach(forbidden_dir IN LISTS SENCHA_TARGET_FORBIDDEN_INCLUDE_DIRS)
        get_filename_component(absolute_forbidden_dir "${forbidden_dir}" ABSOLUTE)
        string(REPLACE "\\" "/" normalized_forbidden_dir "${absolute_forbidden_dir}")
        list(APPEND normalized_forbidden_dirs "${normalized_forbidden_dir}")
    endforeach()

    set(violations)
    foreach(property_name INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(include_entries ${target_name} ${property_name})
        if(NOT include_entries OR include_entries STREQUAL "include_entries-NOTFOUND")
            continue()
        endif()

        foreach(include_entry IN LISTS include_entries)
            if(include_entry MATCHES "^\\$<BUILD_INTERFACE:([^>]+)>$")
                set(include_entry_to_check "${CMAKE_MATCH_1}")
            elseif(include_entry MATCHES "^\\$<INSTALL_INTERFACE:")
                continue()
            elseif(include_entry MATCHES "^\\$<")
                continue()
            else()
                set(include_entry_to_check "${include_entry}")
            endif()

            get_filename_component(absolute_include_entry "${include_entry_to_check}" ABSOLUTE)
            string(REPLACE "\\" "/" normalized_include_entry "${absolute_include_entry}")

            foreach(forbidden_dir IN LISTS normalized_forbidden_dirs)
                string(FIND "${normalized_include_entry}/" "${forbidden_dir}/" forbidden_position)
                if(forbidden_position EQUAL 0)
                    string(APPEND violations
                        "\n  ${target_name}.${property_name} contains forbidden include directory '${include_entry_to_check}'"
                    )
                endif()
            endforeach()
        endforeach()
    endforeach()

    if(violations)
        message(FATAL_ERROR
            "Sencha include visibility boundary violation:${violations}\n"
            "Layer targets may only expose their own include directory plus transitive lower-layer usage requirements."
        )
    endif()
endfunction()
