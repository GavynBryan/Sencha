add_subdirectory(TransformHierarchyStressTest)
add_subdirectory(SenchaInputSystem)

if(SENCHA_ENABLE_VULKAN)
    add_subdirectory(SdlVulkanWindow)
    add_subdirectory(JasmineGreen)
endif()
