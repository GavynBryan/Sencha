add_subdirectory(SenchaInputSystem)

if(SENCHA_ENABLE_VULKAN)
    add_subdirectory(SdlVulkanWindow)
endif()
