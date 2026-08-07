include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Prevent GoogleTest from overriding compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Keep GoogleTest out of the SDK install (engine-roadmap.md: SDK boundary, shipped): it is
# a test-only dependency, not part of the published engine product.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

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

file(GLOB_RECURSE PHYSICS_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/physics/*.cpp
)

file(GLOB_RECURSE BRUSH_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/brush/*.cpp
)

file(GLOB_RECURSE LEVEL_COOK_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/level_cook/*.cpp
)

file(GLOB_RECURSE FRAMEWORK_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cpp
)

file(GLOB_RECURSE EDITOR_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/editor/*.cpp
)

file(GLOB_RECURSE NET_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/net/*.cpp
)

add_executable(core_tests ${CORE_TEST_SOURCES})
target_link_libraries(core_tests PRIVATE sencha_engine GTest::gtest_main)
# Committed content lives in the source tree, not beside the test binary.
target_compile_definitions(core_tests PRIVATE SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")

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
    sencha_warnings(test_game_module)
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

if(BRUSH_TEST_SOURCES AND TARGET kyusu_authoring)
    # The brush geometry kernel is pure (math-only, no UI). Static-library
    # linking pulls only the objects these tests actually reference, so the
    # binary stays as lean as the hand-listed source set it replaced.
    add_executable(brush_tests ${BRUSH_TEST_SOURCES})
    target_link_libraries(brush_tests PRIVATE kyusu_authoring GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS brush_tests)
endif()

if(JOBS_TEST_SOURCES)
    add_executable(jobs_tests ${JOBS_TEST_SOURCES})
    target_link_libraries(jobs_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS jobs_tests)
endif()

# Physics tests link only the engine: the Jolt backend is firewalled inside
# sencha_engine, so the tests see engine physics types and never a JPH header.
if(PHYSICS_TEST_SOURCES)
    add_executable(physics_tests ${PHYSICS_TEST_SOURCES})
    target_link_libraries(physics_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS physics_tests)
endif()

# The level cook is editor-side (it consumes editor brush/level types) but the
# relevant sources are UI/Vulkan-free, so they come from the authoring library.
if(LEVEL_COOK_TEST_SOURCES AND TARGET kyusu_authoring)
    add_executable(level_cook_tests ${LEVEL_COOK_TEST_SOURCES})
    target_link_libraries(level_cook_tests PRIVATE kyusu_authoring GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS level_cook_tests)
endif()

if(FRAMEWORK_TEST_SOURCES)
    add_executable(framework_tests ${FRAMEWORK_TEST_SOURCES})
    target_link_libraries(framework_tests PRIVATE sencha_engine GTest::gtest_main)
    # The template's shipped gameplay data lives in the source tree, so the
    # authored assets can be validated against the schemas that load them.
    target_compile_definitions(framework_tests PRIVATE SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    list(APPEND SENCHA_TEST_TARGETS framework_tests)
endif()

# Editor headless tests. Editor code that is GUI/Vulkan-free (the pure gizmo and
# selection math) is compiled and tested directly, linking only the engine for
# its math headers -- the brush_tests discipline. GUI-coupled editor code (ImGui,
# render passes) is out of scope for headless tests by construction.
if(EDITOR_TEST_SOURCES AND TARGET kyusu_authoring)
    # Kyusu's authoring half arrives as a library; only the other editor
    # applications, which have no such split, are still listed by hand.
    add_executable(editor_tests
        ${EDITOR_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/editor/common/src/ui/EditorThemeFile.cpp
        ${CMAKE_SOURCE_DIR}/editor/shudei/src/MaterialEditSession.cpp
        ${CMAKE_SOURCE_DIR}/editor/shudei/src/MaterialTabSet.cpp
        ${CMAKE_SOURCE_DIR}/editor/shudei/src/PreviewPrimitives.cpp
        ${CMAKE_SOURCE_DIR}/editor/kettle/src/ProjectCatalog.cpp
        # The data editor's GUI-free half: the document's edit transaction and
        # the movement authoring model. Its panels stay out by construction.
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/DataDocument.cpp
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/SubtypeEditorRegistry.cpp
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/input/InputBindingSummary.cpp
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/input/InputControlCapture.cpp
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/movement/MovementLayerSummary.cpp
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src/movement/MovementResponseSim.cpp)
    # editor/ for the shared fonts, and the flat application trees. Kyusu's
    # and editor_common's include paths come with kyusu_authoring, which is also
    # what lets a test include a shell-only header (a panel) without compiling it.
    target_include_directories(editor_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/editor
        ${CMAKE_SOURCE_DIR}/editor/shudei/src
        ${CMAKE_SOURCE_DIR}/editor/kettle/src
        ${CMAKE_SOURCE_DIR}/editor/data_editor/src)
    target_link_libraries(editor_tests PRIVATE kyusu_authoring GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS editor_tests)
endif()

# The transport, protocol, and channel layers. Their own target because they
# need no world, no graphics, and no content: the fuzz and impairment suites run
# in a fraction of a second and are the ones worth running on every change.
if(NET_TEST_SOURCES)
    add_executable(net_tests ${NET_TEST_SOURCES})
    target_link_libraries(net_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS net_tests)
endif()

# Held to the same warning set as the code under test, applied over the
# accumulated list so a test executable added above cannot miss it.
foreach(sencha_test_target IN LISTS SENCHA_TEST_TARGETS)
    sencha_warnings(${sencha_test_target})
endforeach()

add_custom_target(engine_tests DEPENDS ${SENCHA_TEST_TARGETS})

include(GoogleTest)
gtest_discover_tests(core_tests
    DISCOVERY_TIMEOUT 30
)

gtest_discover_tests(math_geometry_tests
    DISCOVERY_TIMEOUT 30
)

if(TARGET net_tests)
    gtest_discover_tests(net_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

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

if(TARGET physics_tests)
    gtest_discover_tests(physics_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET brush_tests)
    gtest_discover_tests(brush_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET editor_tests)
    gtest_discover_tests(editor_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET level_cook_tests)
    gtest_discover_tests(level_cook_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET framework_tests)
    gtest_discover_tests(framework_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

# Static dependency-direction guard for the mesh-edit subsystem. Not a gtest —
# a source scan run as a ctest so `ctest` fails if the layering regresses.
if(NOT WIN32)
    add_test(
        NAME meshedit_dependency_directions
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_meshedit_deps.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME module_abi_surface
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_module_abi.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME ui_color_discipline
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_ui_colors.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME editor_layering_directions
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_editor_layering.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME asset_layering_directions
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_asset_layering.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME gameplay_input_discipline
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_gameplay_input.sh ${CMAKE_SOURCE_DIR}
    )
endif()
