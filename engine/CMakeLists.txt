option(SENCHA_BUILD_INFUSER "Build Sencha Infuser implementation layer" ON)
option(SENCHA_ENABLE_VULKAN "Build Sencha Vulkan support" ON)

include(${CMAKE_SOURCE_DIR}/cmake/SenchaLayerBoundaries.cmake)

set(SENCHA_KETTLE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/kettle/include)
set(SENCHA_TEAPOT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/teapot/include)
set(SENCHA_INFUSER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/infuser/include)

file(GLOB_RECURSE SENCHA_KETTLE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/src/*.cpp
)

file(GLOB_RECURSE SENCHA_KETTLE_HEADERS CONFIGURE_DEPENDS
    ${SENCHA_KETTLE_INCLUDE_DIR}/*.h
)

file(GLOB_RECURSE SENCHA_TEAPOT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/teapot/src/*.cpp
)

file(GLOB_RECURSE SENCHA_TEAPOT_HEADERS CONFIGURE_DEPENDS
    ${SENCHA_TEAPOT_INCLUDE_DIR}/*.h
)

if(SENCHA_BUILD_INFUSER)
    file(GLOB_RECURSE SENCHA_INFUSER_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/infuser/src/*.cpp
    )

    file(GLOB_RECURSE SENCHA_INFUSER_HEADERS CONFIGURE_DEPENDS
        ${SENCHA_INFUSER_INCLUDE_DIR}/*.h
    )

    if(NOT SENCHA_ENABLE_VULKAN)
        list(FILTER SENCHA_INFUSER_SOURCES EXCLUDE REGEX "/infuser/src/vulkan/")
        list(FILTER SENCHA_INFUSER_HEADERS EXCLUDE REGEX "/infuser/include/vulkan/")
    endif()
endif()

sencha_collect_include_prefixes(SENCHA_TEAPOT_INCLUDE_PREFIXES ${SENCHA_TEAPOT_INCLUDE_DIR})
sencha_collect_include_prefixes(SENCHA_INFUSER_INCLUDE_PREFIXES ${SENCHA_INFUSER_INCLUDE_DIR})
set(SENCHA_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES SDL3/ vulkan/)

set(SENCHA_KETTLE_FORBIDDEN_INCLUDE_PREFIXES
    ${SENCHA_TEAPOT_INCLUDE_PREFIXES}
    ${SENCHA_INFUSER_INCLUDE_PREFIXES}
    ${SENCHA_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES}
)
list(REMOVE_DUPLICATES SENCHA_KETTLE_FORBIDDEN_INCLUDE_PREFIXES)

set(SENCHA_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES
    ${SENCHA_INFUSER_INCLUDE_PREFIXES}
    ${SENCHA_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES}
)
list(REMOVE_DUPLICATES SENCHA_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES)

sencha_assert_layer_boundary(kettle
    FILES ${SENCHA_KETTLE_SOURCES} ${SENCHA_KETTLE_HEADERS}
    FORBIDDEN_INCLUDE_PREFIXES ${SENCHA_KETTLE_FORBIDDEN_INCLUDE_PREFIXES}
    FORBIDDEN_PATH_TOKENS teapot infuser
)

add_library(sencha_kettle STATIC ${SENCHA_KETTLE_SOURCES} ${SENCHA_KETTLE_HEADERS})
add_library(sencha::kettle ALIAS sencha_kettle)
target_include_directories(sencha_kettle PUBLIC ${SENCHA_KETTLE_INCLUDE_DIR})

sencha_assert_layer_boundary(teapot
    FILES ${SENCHA_TEAPOT_SOURCES} ${SENCHA_TEAPOT_HEADERS}
    FORBIDDEN_INCLUDE_PREFIXES ${SENCHA_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES}
    FORBIDDEN_PATH_TOKENS infuser
)

add_library(sencha_teapot STATIC ${SENCHA_TEAPOT_SOURCES} ${SENCHA_TEAPOT_HEADERS})
add_library(sencha::teapot ALIAS sencha_teapot)
target_include_directories(sencha_teapot PUBLIC ${SENCHA_TEAPOT_INCLUDE_DIR})
target_link_libraries(sencha_teapot PUBLIC sencha_kettle)

if(SENCHA_BUILD_INFUSER)
    find_package(SDL3 REQUIRED CONFIG)

    add_library(sencha_infuser STATIC ${SENCHA_INFUSER_SOURCES} ${SENCHA_INFUSER_HEADERS})
    add_library(sencha::infuser ALIAS sencha_infuser)
    target_include_directories(sencha_infuser PUBLIC ${SENCHA_INFUSER_INCLUDE_DIR})
    target_link_libraries(sencha_infuser PUBLIC sencha_teapot SDL3::SDL3)

    if(SENCHA_ENABLE_VULKAN)
        find_package(Vulkan REQUIRED)
        target_link_libraries(sencha_infuser PUBLIC Vulkan::Vulkan)
    endif()
endif()

sencha_assert_target_does_not_link(sencha_kettle
    FORBIDDEN_TARGETS
        engine
        sencha_teapot
        sencha::teapot
        sencha_infuser
        sencha::infuser
        SDL3::SDL3
        Vulkan::Vulkan
)

sencha_assert_target_does_not_link(sencha_teapot
    FORBIDDEN_TARGETS
        engine
        sencha_infuser
        sencha::infuser
        SDL3::SDL3
        Vulkan::Vulkan
)

sencha_assert_target_does_not_include(sencha_kettle
    FORBIDDEN_INCLUDE_DIRS
        ${SENCHA_TEAPOT_INCLUDE_DIR}
        ${SENCHA_INFUSER_INCLUDE_DIR}
)

sencha_assert_target_does_not_include(sencha_teapot
    FORBIDDEN_INCLUDE_DIRS
        ${SENCHA_INFUSER_INCLUDE_DIR}
)

# Compatibility aggregate for apps/examples. With default options this means the
# full stack through Infuser; layer code should link the exact layer target.
add_library(engine INTERFACE)
if(SENCHA_BUILD_INFUSER)
    add_library(sencha_full_stack INTERFACE)
    add_library(sencha::full_stack ALIAS sencha_full_stack)
    target_link_libraries(sencha_full_stack INTERFACE sencha_infuser)
    target_link_libraries(engine INTERFACE sencha_full_stack)
else()
    target_link_libraries(engine INTERFACE sencha_teapot)
endif()
