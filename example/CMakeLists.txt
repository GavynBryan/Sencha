add_subdirectory(TransformHierarchyStressTest)
add_subdirectory(AudioTest)
if(SENCHA_ENABLE_VULKAN)
    add_subdirectory(CubeDemo)
endif()

if(SENCHA_ENABLE_VULKAN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JasmineGreen/CMakeLists.txt")
    add_subdirectory(JasmineGreen)
endif()
