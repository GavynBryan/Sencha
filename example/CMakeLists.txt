add_subdirectory(TransformHierarchyStressTest)
add_subdirectory(AudioTest)

if(SENCHA_ENABLE_VULKAN)
    add_subdirectory(JasmineGreen)
    add_subdirectory(QuadTreeDemo)
endif()
