include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Prevent GoogleTest from overriding compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

file(GLOB_RECURSE CORE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
)

file(GLOB_RECURSE MATH_GEOMETRY_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/math_geometry/*.cpp
)

file(GLOB_RECURSE ENGINE_FEATURE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine_features/*.cpp
)

file(GLOB_RECURSE RUNTIME_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp
)

add_executable(core_tests ${CORE_TEST_SOURCES})
target_link_libraries(core_tests PRIVATE sencha_engine GTest::gtest_main)

add_executable(math_geometry_tests ${MATH_GEOMETRY_TEST_SOURCES})
target_link_libraries(math_geometry_tests PRIVATE sencha_engine GTest::gtest_main)

set(SENCHA_TEST_TARGETS core_tests math_geometry_tests)

if(RUNTIME_TEST_SOURCES)
    add_executable(runtime_tests ${RUNTIME_TEST_SOURCES})
    target_link_libraries(runtime_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS runtime_tests)
endif()

if(ENGINE_FEATURE_TEST_SOURCES)
    add_executable(engine_feature_tests ${ENGINE_FEATURE_TEST_SOURCES})
    target_link_libraries(engine_feature_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS engine_feature_tests)
endif()

add_custom_target(engine_tests DEPENDS ${SENCHA_TEST_TARGETS})

include(GoogleTest)
gtest_discover_tests(core_tests
    DISCOVERY_TIMEOUT 30
)

gtest_discover_tests(math_geometry_tests
    DISCOVERY_TIMEOUT 30
)

if(TARGET engine_feature_tests)
    gtest_discover_tests(engine_feature_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET runtime_tests)
    gtest_discover_tests(runtime_tests
        DISCOVERY_TIMEOUT 30
    )
endif()
