if(SENCHA_BUILD_INFUSER)
    add_subdirectory(TransformHierarchyStressTest)
    add_subdirectory(SenchaInputSystem)
endif()

if(SENCHA_BUILD_INFUSER AND SENCHA_ENABLE_VULKAN)
    add_subdirectory(SdlVulkanWindow)
    add_subdirectory(JasmineGreen)
endif()
