# In-tree only. Builds each starter template named in SENCHA_BUILD_TEMPLATES
# against the engine being built, so a template always carries the host's ABI
# fingerprint during engine development. A template's own CMakeLists.txt is the
# standalone front door; this file is never installed and no template refers to
# it.
foreach(_template IN LISTS SENCHA_BUILD_TEMPLATES)
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_template}")
        message(FATAL_ERROR
            "SENCHA_BUILD_TEMPLATES names '${_template}', which is not a directory under templates/")
    endif()
    add_subdirectory(${_template})
endforeach()
