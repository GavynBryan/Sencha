option(SENCHA_ENABLE_VULKAN "Build Sencha Vulkan support" ON)

file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/teapot/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/infuser/src/*.cpp
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/teapot/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/infuser/include/*.h
)

if(NOT SENCHA_ENABLE_VULKAN)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/infuser/src/vulkan/")
    list(FILTER ENGINE_HEADERS EXCLUDE REGEX "/infuser/include/vulkan/")
endif()

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/include
    ${CMAKE_CURRENT_SOURCE_DIR}/teapot/include
    ${CMAKE_CURRENT_SOURCE_DIR}/infuser/include
)

find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(engine PUBLIC SDL3::SDL3)

if(SENCHA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(engine PUBLIC Vulkan::Vulkan)
endif()
