# Diagnostic flags for first-party targets.
#
# One owner for the warning set so targets cannot drift: every first-party
# library and executable calls sencha_warnings(<target>) and gets the same bar.
# Third-party targets never call it. Third-party implementation bodies that are
# compiled inside a first-party translation unit (stb, cgltf, rgbcx, VMA) are
# pragma-guarded at their include site instead -- see
# engine/src/graphics/vulkan/VulkanMemoryAllocatorImpl.cpp for the pattern.
#
# This file is deliberately not installed with the SDK. An out-of-tree game
# module picks its own diagnostics; cmake/SenchaGameModule.cmake imposes none.
include_guard(GLOBAL)

# PRIVATE throughout: diagnostics describe how we compile our own sources, and
# must not ride along on anything that links the target.
function(sencha_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wshadow;-Wnon-virtual-dtor>
        $<$<CXX_COMPILER_ID:MSVC>:/W4;/permissive->
    )

    if(SENCHA_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang>:-Werror>
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
        )
    endif()
endfunction()
