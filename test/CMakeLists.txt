include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Prevent GoogleTest from overriding compiler/linker options on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/kettle/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp
)

add_executable(engine_tests ${TEST_SOURCES})

target_link_libraries(engine_tests PRIVATE engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(engine_tests)
