option(SENCHA_ENABLE_VULKAN "Build Sencha Vulkan support" ON)

set(SENCHA_ENGINE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(SENCHA_ENGINE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SENCHA_ENGINE_SOURCES CONFIGURE_DEPENDS
    ${SENCHA_ENGINE_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE SENCHA_ENGINE_HEADERS CONFIGURE_DEPENDS
    ${SENCHA_ENGINE_INCLUDE_DIR}/*.h
)

if(NOT SENCHA_ENABLE_VULKAN)
    list(FILTER SENCHA_ENGINE_SOURCES EXCLUDE REGEX "[/\\\\]src[/\\\\]render[/\\\\]backend[/\\\\]vulkan[/\\\\]")
    list(FILTER SENCHA_ENGINE_HEADERS EXCLUDE REGEX "[/\\\\]include[/\\\\]render[/\\\\]backend[/\\\\]vulkan[/\\\\]")
endif()

find_package(SDL3 REQUIRED CONFIG)

add_library(sencha_engine STATIC
    ${SENCHA_ENGINE_SOURCES}
    ${SENCHA_ENGINE_HEADERS}
)
add_library(sencha::engine ALIAS sencha_engine)

target_include_directories(sencha_engine PUBLIC ${SENCHA_ENGINE_INCLUDE_DIR})
target_link_libraries(sencha_engine PUBLIC SDL3::SDL3)

if(SENCHA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(sencha_engine PUBLIC Vulkan::Vulkan)

    include(FetchContent)
    set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(VMA_BUILD_SAMPLES       OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.2.1
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)
    target_link_libraries(sencha_engine PUBLIC GPUOpen::VulkanMemoryAllocator)
endif()

# Compatibility target for apps/examples that already link `engine`.
add_library(engine INTERFACE)
target_link_libraries(engine INTERFACE sencha_engine)
