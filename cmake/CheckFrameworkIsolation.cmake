# Enforces the D-J dependency rule for the gameplay framework:
#   - framework code must NOT include the renderer, graphics, audio, or the
#     render/audio-pulling scene codecs (gameplay is decoupled from render/scene)
#   - engine code outside framework/ must NOT include framework/ (engine never
#     depends on gameplay)
#
# Run standalone (no build/Vulkan needed):
#   cmake -P cmake/CheckFrameworkIsolation.cmake
# Or via the check_framework_isolation target once configured.

cmake_minimum_required(VERSION 3.20)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Includes a framework/ file may never pull (substrings matched after "#include <").
set(FRAMEWORK_FORBIDDEN
    "render/"
    "graphics/"
    "audio/"
    "world/serialization/SceneFieldCodec.h"
    "world/serialization/ComponentSerializer.h"
    "world/serialization/SceneSerializer.h"
)

set(VIOLATIONS "")

# 1. framework/ must not reach into render/scene.
file(GLOB_RECURSE FRAMEWORK_FILES
    "${REPO}/engine/include/framework/*"
    "${REPO}/engine/src/framework/*"
)
foreach(file ${FRAMEWORK_FILES})
    file(READ "${file}" content)
    file(RELATIVE_PATH rel "${REPO}" "${file}")
    foreach(bad ${FRAMEWORK_FORBIDDEN})
        string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]${bad}" hit "${content}")
        if(hit)
            list(APPEND VIOLATIONS "${rel} includes '${bad}' (framework must not depend on render/scene)")
        endif()
    endforeach()
endforeach()

# 2. engine code outside framework/ must not depend on framework/.
file(GLOB_RECURSE ENGINE_FILES
    "${REPO}/engine/include/*"
    "${REPO}/engine/src/*"
)
foreach(file ${ENGINE_FILES})
    if(file MATCHES "/framework/")
        continue()
    endif()
    file(READ "${file}" content)
    string(REGEX MATCH "#[ \t]*include[ \t]*[<\"]framework/" hit "${content}")
    if(hit)
        file(RELATIVE_PATH rel "${REPO}" "${file}")
        list(APPEND VIOLATIONS "${rel} includes 'framework/' (engine must not depend on the framework)")
    endif()
endforeach()

if(VIOLATIONS)
    foreach(v ${VIOLATIONS})
        message(WARNING "framework isolation: ${v}")
    endforeach()
    list(LENGTH VIOLATIONS n)
    message(FATAL_ERROR "framework isolation check failed: ${n} violation(s)")
endif()

list(LENGTH FRAMEWORK_FILES fw_count)
message(STATUS "framework isolation OK (${fw_count} framework files clean)")
