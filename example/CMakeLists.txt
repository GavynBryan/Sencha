add_subdirectory(TransformHierarchyStressTest)

if(SENCHA_BUILD_INFUSER)
    add_subdirectory(SenchaInputSystem)
endif()

if(SENCHA_BUILD_INFUSER AND SENCHA_ENABLE_VULKAN)
    add_subdirectory(SdlVulkanWindow)
endif()
