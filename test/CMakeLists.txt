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
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMesh.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/FaceMaterial.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushTessellation.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMeshSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMeshStore.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushValidation.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushHalfEdge.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushOps.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshElements.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshEditService.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshElementKindTraits.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/ElementGeometry.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/overlay/SelectionLabels.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/SelectionContext.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/SelectionFold.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/SelectionService.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/commands/SelectCommand.cpp)
    target_include_directories(brush_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src
        ${CMAKE_SOURCE_DIR}/editor/common/src)
    target_link_libraries(brush_tests PRIVATE sencha_engine GTest::gtest_main)
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
# relevant sources are UI/Vulkan-free, so the tests compile them directly and
# link only the engine (same discipline as brush_tests).
if(LEVEL_COOK_TEST_SOURCES)
    add_executable(level_cook_tests
        ${LEVEL_COOK_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/AssetFieldIo.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/BrushBake.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/DocumentCook.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/EditorDocument.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/EditorScene.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/DocumentSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/BrushCookInput.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/commands/DeleteEntityCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/commands/DuplicateEntitiesCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/commands/MergeBrushesCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/document/commands/SeparateFacesCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/export/GltfMeshExport.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/SelectionContext.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/SelectionService.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/selection/commands/SelectCommand.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMesh.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/FaceMaterial.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushTessellation.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMeshSerialization.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushMeshStore.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushValidation.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushHalfEdge.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushOps.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/brush/BrushBounds.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshElements.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshEditService.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/MeshElementKindTraits.cpp
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src/meshedit/ElementGeometry.cpp)
    target_include_directories(level_cook_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src
        ${CMAKE_SOURCE_DIR}/editor/common/src)
    target_link_libraries(level_cook_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS level_cook_tests)
endif()

if(FRAMEWORK_TEST_SOURCES)
    add_executable(framework_tests ${FRAMEWORK_TEST_SOURCES})
    target_link_libraries(framework_tests PRIVATE sencha_engine GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS framework_tests)
endif()

# Editor headless tests. Editor code that is GUI/Vulkan-free (the pure gizmo and
# selection math) is compiled and tested directly, linking only the engine for
# its math headers -- the brush_tests discipline. GUI-coupled editor code (ImGui,
# render passes) is out of scope for headless tests by construction.
if(EDITOR_TEST_SOURCES)
    # EditorThemeFile is UI-adjacent but headless (it needs ImGui's ImVec4 type,
    # not its runtime), so imgui_support rides along for the headers.
    add_executable(editor_tests
        ${EDITOR_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/editor/common/src/commands/CommandStack.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/input/KeymapFile.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/project/MaterialLibrary.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/project/Project.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/project/ProjectArgs.cpp
        ${CMAKE_SOURCE_DIR}/editor/common/src/ui/EditorThemeFile.cpp
        ${CMAKE_SOURCE_DIR}/editor/chakin/src/MaterialEditSession.cpp
        ${CMAKE_SOURCE_DIR}/editor/chakin/src/MaterialTabSet.cpp
        ${CMAKE_SOURCE_DIR}/editor/chakin/src/PreviewPrimitives.cpp
        ${CMAKE_SOURCE_DIR}/editor/kettle/src/ProjectCatalog.cpp)
    target_include_directories(editor_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/editor/kyusu/src
        ${CMAKE_SOURCE_DIR}/editor/common/src
        ${CMAKE_SOURCE_DIR}/editor/chakin/src
        ${CMAKE_SOURCE_DIR}/editor/kettle/src)
    target_link_libraries(editor_tests PRIVATE sencha_engine sencha_imgui_support GTest::gtest_main)
    list(APPEND SENCHA_TEST_TARGETS editor_tests)
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
endif()
