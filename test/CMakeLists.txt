include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Prevent GoogleTest from overriding compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# Keep GoogleTest out of the SDK install (real-engine-roadmap.md Phase 0): it is
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

file(GLOB_RECURSE BRUSH_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/brush/*.cpp
)

file(GLOB_RECURSE LEVEL_COOK_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/level_cook/*.cpp
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

if(BRUSH_TEST_SOURCES)
    # The brush geometry kernel is pure (math-only, no UI), so the tests compile
    # its sources directly and link only the engine — no editor/Vulkan/ImGui deps.
    add_executable(brush_tests
        ${BRUSH_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMesh.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/FaceMaterial.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushTessellation.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMeshSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMeshStore.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushValidation.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushHalfEdge.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushOps.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshElements.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshEditService.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshElementKindTraits.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/ElementGeometry.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/SelectionContext.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/SelectionService.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/commands/SelectCommand.cpp)
    target_include_directories(brush_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/src)
    target_link_libraries(brush_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS brush_tests)
endif()

if(JOBS_TEST_SOURCES)
    add_executable(jobs_tests ${JOBS_TEST_SOURCES})
    target_link_libraries(jobs_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS jobs_tests)
endif()

# The level cook is editor-side (it consumes editor brush/level types) but the
# relevant sources are UI/Vulkan-free, so the tests compile them directly and
# link only the engine (same discipline as brush_tests).
if(LEVEL_COOK_TEST_SOURCES)
    add_executable(level_cook_tests
        ${LEVEL_COOK_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/editor/src/document/AssetFieldIo.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/DocumentCook.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/EditorDocument.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/EditorScene.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/DocumentSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/BrushCookInput.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/document/commands/DeleteEntityCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/SelectionContext.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/SelectionService.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/selection/commands/SelectCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMesh.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/FaceMaterial.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushTessellation.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMeshSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushMeshStore.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushValidation.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushHalfEdge.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushOps.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/brush/BrushBounds.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshElements.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshEditService.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/MeshElementKindTraits.cpp
        ${CMAKE_SOURCE_DIR}/editor/src/meshedit/ElementGeometry.cpp)
    target_include_directories(level_cook_tests PRIVATE ${CMAKE_SOURCE_DIR}/editor/src)
    target_link_libraries(level_cook_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS level_cook_tests)
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

if(TARGET brush_tests)
    gtest_discover_tests(brush_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET level_cook_tests)
    gtest_discover_tests(level_cook_tests
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
endif()
