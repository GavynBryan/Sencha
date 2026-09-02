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

file(GLOB_RECURSE PARTICIPANT_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/participant/*.cpp
)

add_executable(core_tests ${CORE_TEST_SOURCES})
target_link_libraries(core_tests PRIVATE sencha_engine GTest::gtest_main)
# The component-codegen golden fixture is included by logical path.
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Committed content lives in the source tree, not beside the test binary.
target_compile_definitions(core_tests PRIVATE SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
get_target_property(_engine_component_index sencha_engine SENCHA_COMPONENT_INDEX)
target_compile_definitions(core_tests PRIVATE
    SENCHA_ENGINE_COMPONENT_INDEX="${_engine_component_index}")

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

# Two-process hosting coverage runs the shipping host binary against the
# template game module, so it needs both built and needs to know where they
# landed. Without them the cases skip rather than fail: a configuration that
# builds no template has nothing to host.
if(TARGET runtime_tests AND TARGET app AND TARGET template_game)
    add_dependencies(runtime_tests app template_game)
    target_compile_definitions(runtime_tests PRIVATE
        TEST_APP_PATH="$<TARGET_FILE:app>"
        TEST_TEMPLATE_MODULE_PATH="$<TARGET_FILE:template_game>"
        SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
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
    add_executable(level_cook_tests ${LEVEL_COOK_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/template/src/TemplateComponents.cpp)
    target_include_directories(level_cook_tests PRIVATE ${CMAKE_SOURCE_DIR}/template/src)
    target_link_libraries(level_cook_tests PRIVATE kyusu_authoring GTest::gtest_main)
    # The template's components are compiled here, so their metadata is
    # generated here, as the module generates it for itself.
    sencha_generate_component_metadata(level_cook_tests
        INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/template/src
        HEADERS ${CMAKE_SOURCE_DIR}/template/src/ObserverFlight.h
                ${CMAKE_SOURCE_DIR}/template/src/PlayerStartComponent.h
                ${CMAKE_SOURCE_DIR}/template/src/SpinComponent.h
                ${CMAKE_SOURCE_DIR}/template/src/TurretMount.h)
    # The shipped prefabs are content under test: a case reads them from the
    # checkout rather than restating them.
    target_compile_definitions(level_cook_tests PRIVATE
        SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    list(APPEND SENCHA_TEST_TARGETS level_cook_tests)
endif()

if(FRAMEWORK_TEST_SOURCES)
    # The template owns the player.avatar subtype, and its compiler is pure data
    # code over engine headers. Compiling that one translation unit here tests it
    # where it lives rather than duplicating the schema (the brush_tests and
    # editor_tests discipline: link the GUI-free code directly).
    # Same discipline for the examples' schedule composition: link the
    # GUI-free registration TUs directly so a bad After edge fails under ctest
    # instead of only on a Debug boot.
    #
    # One FreeCamera.cpp for both. example/CubeDemo and example/SceneViewer
    # carry byte-identical copies of FreeCamera.{h,cpp}, so a single definition
    # resolves both systems TUs; linking both would be a duplicate symbol. If
    # the two ever diverge, this needs to become two test executables.
    add_executable(framework_tests
        ${FRAMEWORK_TEST_SOURCES}
        ${CMAKE_SOURCE_DIR}/template/src/GameSettingsData.cpp
        ${CMAKE_SOURCE_DIR}/template/src/PlayerAvatarData.cpp
        ${CMAKE_SOURCE_DIR}/example/CubeDemo/CubeDemoSystems.cpp
        ${CMAKE_SOURCE_DIR}/example/CubeDemo/CubeDemoScene.cpp
        ${CMAKE_SOURCE_DIR}/example/CubeDemo/FreeCamera.cpp
        ${CMAKE_SOURCE_DIR}/example/SceneViewer/SceneViewerSystems.cpp)
    target_include_directories(framework_tests PRIVATE ${CMAKE_SOURCE_DIR}/template/src)
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

# The renderer's only pixel-level coverage: renders scenes through the shipped
# host and compares against committed reference images. Its own target because
# it is the one suite that needs a GPU and a display, and because it drives a
# separate process rather than linking what it tests. It skips, rather than
# fails, where it cannot run.
file(GLOB_RECURSE RENDER_GOLDEN_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/render_golden/*.cpp
)
if(RENDER_GOLDEN_TEST_SOURCES AND SENCHA_ENABLE_VULKAN)
    add_executable(render_golden_tests ${RENDER_GOLDEN_TEST_SOURCES})
    target_link_libraries(render_golden_tests PRIVATE sencha_engine GTest::gtest_main)
    target_compile_definitions(render_golden_tests PRIVATE
        # The bundled host, not the bare target: `app` discovers its game module
        # as game<ext> beside itself, and only the bundle directory has one.
        SENCHA_GOLDEN_APP="$<TARGET_FILE_DIR:SceneViewer>/app"
        SENCHA_GOLDEN_CONTENT_ROOT="${CMAKE_SOURCE_DIR}/template"
        SENCHA_GOLDEN_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/render_golden/references"
        SENCHA_GOLDEN_COOK="$<TARGET_FILE:level_cook_tests>"
        SENCHA_GOLDEN_EDITOR="$<TARGET_FILE:kyusu>")
    # The host, the default game module, and the cook have to exist before a
    # scene renders; the test cooks its own fixtures.
    add_dependencies(render_golden_tests app SceneViewer level_cook_tests kyusu)
    list(APPEND SENCHA_TEST_TARGETS render_golden_tests)
endif()

if(PARTICIPANT_TEST_SOURCES)
    add_executable(participant_tests ${PARTICIPANT_TEST_SOURCES})
    target_link_libraries(participant_tests PRIVATE sencha_engine GTest::gtest_main)
    target_compile_definitions(participant_tests PRIVATE
        SENCHA_REPO_ROOT="${CMAKE_SOURCE_DIR}")
    list(APPEND SENCHA_TEST_TARGETS participant_tests)
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

if(TARGET participant_tests)
    gtest_discover_tests(participant_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

if(TARGET render_golden_tests)
    # Longer discovery and run: each case launches the host and renders a scene.
    gtest_discover_tests(render_golden_tests
        DISCOVERY_TIMEOUT 60
    )
endif()

if(TARGET engine_feature_tests)
    gtest_discover_tests(engine_feature_tests
        DISCOVERY_TIMEOUT 30
    )
endif()

# The map the two-process hosting cases load is cooked output. A checkout
# carries the authored level and not the cook of it, and no build step produces
# one, so that coverage only ever ran where an earlier local cook happened to
# have left a scene behind -- and reported success everywhere it had not.
#
# Cooked by the level-cook test binary rather than from inside runtime_tests:
# the cook lives in the editor authoring library, and a runtime test binary must
# not link that.
set(SENCHA_HOST_MAP_FIXTURE "")
if(TARGET runtime_tests AND TARGET app AND TARGET template_game
   AND TARGET level_cook_tests)
    set(SENCHA_HOST_MAP_FIXTURE TemplateHostMap)
    add_test(NAME CookTemplateHostMap
        COMMAND level_cook_tests --gtest_filter=CookLevel.Generate)
    set_tests_properties(CookTemplateHostMap PROPERTIES
        FIXTURES_SETUP ${SENCHA_HOST_MAP_FIXTURE}
        ENVIRONMENT
            "SENCHA_COOK_LEVEL=${CMAKE_SOURCE_DIR}/template/assets/levels/room_2.sscene,${CMAKE_SOURCE_DIR}/template/assets/prefabs/player_pawn.sscene,${CMAKE_SOURCE_DIR}/template/assets/prefabs/turret.sscene;SENCHA_COOK_ROOT=${CMAKE_SOURCE_DIR}/template/assets")
    # Tells the cases apart: with the fixture wired a missing scene is a
    # failure, and without it they have no way to get one and skip instead.
    target_compile_definitions(runtime_tests PRIVATE SENCHA_TEST_HOST_MAP_COOKED=1)
endif()

# The fixture requirement lands on every runtime case rather than only the
# hosting ones: gtest_discover_tests names tests at build time, so a property
# set here cannot pick out a suite. The cost is that a cook failure leaves the
# rest of this target unrun -- loud, and pointing at the right thing.
if(TARGET runtime_tests)
    if(SENCHA_HOST_MAP_FIXTURE)
        gtest_discover_tests(runtime_tests
            DISCOVERY_TIMEOUT 30
            PROPERTIES FIXTURES_REQUIRED ${SENCHA_HOST_MAP_FIXTURE}
        )
    else()
        gtest_discover_tests(runtime_tests
            DISCOVERY_TIMEOUT 30
        )
    endif()
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
        NAME component_codegen
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_component_codegen.sh
                $<TARGET_FILE:sencha::component-codegen>
                ${CMAKE_SOURCE_DIR}
                ${CMAKE_BINARY_DIR}/engine/generated-public
    )
    add_test(
        NAME component_index_validation
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_component_index.sh ${CMAKE_SOURCE_DIR}
    )
    add_test(
        NAME gameplay_input_discipline
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_gameplay_input.sh ${CMAKE_SOURCE_DIR}
    )
    # Ran only in the CI workflow until now, so a local `ctest` never saw a
    # portability regression. It reads source and needs no build.
    add_test(
        NAME render_portability
        COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_render_portability.sh ${CMAKE_SOURCE_DIR}
    )
endif()
