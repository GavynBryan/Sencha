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
foreach(_removed_ecs_test
    "/core/DataBatchTests.cpp$"
    "/core/InstanceRegistryTests.cpp$"
)
    list(FILTER CORE_TEST_SOURCES EXCLUDE REGEX "${_removed_ecs_test}")
endforeach()

file(GLOB_RECURSE MATH_GEOMETRY_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/math_geometry/*.cpp
)

file(GLOB_RECURSE ENGINE_FEATURE_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine_features/*.cpp
)

file(GLOB_RECURSE RUNTIME_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp
)

file(GLOB_RECURSE ECS_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.cpp
)

file(GLOB_RECURSE JOBS_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/jobs/*.cpp
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

    # A real loadable game module for the S2 boundary integration test. MODULE so
    # it is dlopen-only; hidden visibility — only SenchaCreateGameModule exported.
    add_library(test_game_module MODULE module/TestGameModule.cpp)
    target_link_libraries(test_game_module PRIVATE sencha_engine)
    set_target_properties(test_game_module PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        PREFIX "")
    add_dependencies(runtime_tests test_game_module)
    target_compile_definitions(runtime_tests PRIVATE
        TEST_GAME_MODULE_PATH="$<TARGET_FILE:test_game_module>"
        TEST_ENGINE_LIB_PATH="$<TARGET_FILE:sencha_engine>")
endif()

if(ENGINE_FEATURE_TEST_SOURCES)
    add_executable(engine_feature_tests ${ENGINE_FEATURE_TEST_SOURCES})
    target_link_libraries(engine_feature_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS engine_feature_tests)
endif()

if(ECS_TEST_SOURCES)
    add_executable(ecs_tests ${ECS_TEST_SOURCES})
    target_link_libraries(ecs_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS ecs_tests)
endif()

if(JOBS_TEST_SOURCES)
    add_executable(jobs_tests ${JOBS_TEST_SOURCES})
    target_link_libraries(jobs_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS jobs_tests)
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

if(TARGET ecs_tests)
    gtest_discover_tests(ecs_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET jobs_tests)
    gtest_discover_tests(jobs_tests
        DISCOVERY_TIMEOUT 30
    )
endif()
