include(FetchContent)
include(${CMAKE_SOURCE_DIR}/cmake/SenchaLayerBoundaries.cmake)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Prevent GoogleTest from overriding compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE KETTLE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/*.cpp
)

file(GLOB_RECURSE TEAPOT_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/teapot/*.cpp
)

if(SENCHA_BUILD_INFUSER)
    file(GLOB_RECURSE INFUSER_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/infuser/*.cpp
    )
endif()

sencha_collect_include_prefixes(TEST_TEAPOT_INCLUDE_PREFIXES ${CMAKE_SOURCE_DIR}/engine/teapot/include)
sencha_collect_include_prefixes(TEST_INFUSER_INCLUDE_PREFIXES ${CMAKE_SOURCE_DIR}/engine/infuser/include)
set(TEST_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES SDL3/ vulkan/)

set(TEST_KETTLE_FORBIDDEN_INCLUDE_PREFIXES
    ${TEST_TEAPOT_INCLUDE_PREFIXES}
    ${TEST_INFUSER_INCLUDE_PREFIXES}
    ${TEST_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES}
)
list(REMOVE_DUPLICATES TEST_KETTLE_FORBIDDEN_INCLUDE_PREFIXES)

set(TEST_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES
    ${TEST_INFUSER_INCLUDE_PREFIXES}
    ${TEST_IMPLEMENTATION_EXTERNAL_INCLUDE_PREFIXES}
)
list(REMOVE_DUPLICATES TEST_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES)

sencha_assert_layer_boundary(kettle_tests
    FILES ${KETTLE_TEST_SOURCES}
    FORBIDDEN_INCLUDE_PREFIXES ${TEST_KETTLE_FORBIDDEN_INCLUDE_PREFIXES}
    FORBIDDEN_PATH_TOKENS teapot infuser
)

sencha_assert_layer_boundary(teapot_tests
    FILES ${TEAPOT_TEST_SOURCES}
    FORBIDDEN_INCLUDE_PREFIXES ${TEST_TEAPOT_FORBIDDEN_INCLUDE_PREFIXES}
    FORBIDDEN_PATH_TOKENS infuser
)

add_executable(kettle_tests ${KETTLE_TEST_SOURCES})
target_link_libraries(kettle_tests PRIVATE sencha_kettle GTest::gtest_main)

add_executable(teapot_tests ${TEAPOT_TEST_SOURCES})
target_link_libraries(teapot_tests PRIVATE sencha_teapot GTest::gtest_main)

set(SENCHA_TEST_TARGETS kettle_tests teapot_tests)

if(SENCHA_BUILD_INFUSER AND INFUSER_TEST_SOURCES)
    add_executable(infuser_tests ${INFUSER_TEST_SOURCES})
    target_link_libraries(infuser_tests PRIVATE sencha_infuser GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS infuser_tests)
endif()

add_custom_target(engine_tests DEPENDS ${SENCHA_TEST_TARGETS})

sencha_assert_target_does_not_link(kettle_tests
    FORBIDDEN_TARGETS
        engine
        sencha_teapot
        sencha::teapot
        sencha_infuser
        sencha::infuser
        SDL3::SDL3
        Vulkan::Vulkan
)

sencha_assert_target_does_not_link(teapot_tests
    FORBIDDEN_TARGETS
        engine
        sencha_infuser
        sencha::infuser
        SDL3::SDL3
        Vulkan::Vulkan
)

sencha_assert_target_does_not_include(kettle_tests
    FORBIDDEN_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/engine/teapot/include
        ${CMAKE_SOURCE_DIR}/engine/infuser/include
)

sencha_assert_target_does_not_include(teapot_tests
    FORBIDDEN_INCLUDE_DIRS
        ${CMAKE_SOURCE_DIR}/engine/infuser/include
)

include(GoogleTest)
gtest_discover_tests(kettle_tests
    DISCOVERY_TIMEOUT 30
)

gtest_discover_tests(teapot_tests
    DISCOVERY_TIMEOUT 30
)

if(TARGET infuser_tests)
    gtest_discover_tests(infuser_tests
        DISCOVERY_TIMEOUT 30
    )
endif()
