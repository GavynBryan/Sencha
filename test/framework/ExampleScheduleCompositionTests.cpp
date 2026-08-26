// Composes each example's schedule the way its game module does, without a
// window, a device, or an engine.
//
// EngineSchedule::After asserts on an edge whose endpoints share no phase list
// or are not registered yet. Both faults are Debug-only and neither is
// reachable from any other test, because nothing else builds an example's
// schedule -- which is how three bad edges and a declare-before-register
// shipped and sat unnoticed from 2026-08-04 until a Debug boot tripped over
// them. These tests are the venue that was missing.
//
// Under NDEBUG the asserts compile out and an invalid declaration is a silent
// no-op, so this coverage is meaningful on the Debug/CI leg specifically.

#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <assets/data/DataAssetCache.h>
#include <assets/data/DataAssetTypeRegistry.h>
#include <core/logging/LoggingProvider.h>
#include <core/metadata/DataSchema.h>
#include <input/InputProfileData.h>
#include <input/InputRegistration.h>

#include "../../example/CubeDemo/CubeDemoScene.h"
#include "../../example/CubeDemo/CubeDemoSystems.h"
#include "../../example/CubeDemo/FreeCamera.h"
#include "../../example/SceneViewer/SceneViewerSystems.h"

namespace
{

// Only what the registration functions read: they declare systems and edges,
// and touch no world state.
struct CompositionHarness
{
    LoggingProvider Logging;
    DataAssetTypeRegistry Types;
    DataSchemaRegistry Schemas;
    DataAssetCache Cache;
    EngineSchedule Schedule;

    CompositionHarness()
    {
        RegisterInputProfileData(Types, Schemas);
        // Every example registers the resolve system first; the camera edges
        // name it, and After asserts on an unregistered endpoint.
        RegisterInputSystems(Schedule, Cache, Logging);
    }
};

} // namespace

TEST(ExampleScheduleComposition, CubeDemoDeclaresOnlyEdgesThatOrderSomething)
{
    CompositionHarness harness;
    ::FreeCamera camera;
    DemoScene scene;

    RegisterCubeDemoSystems(harness.Schedule, camera, scene, nullptr);
    harness.Schedule.Init();

    SUCCEED() << "composition declared no cross-phase or unregistered edge";
}

TEST(ExampleScheduleComposition, SceneViewerDeclaresOnlyEdgesThatOrderSomething)
{
    CompositionHarness harness;
    FreeCamera camera;
    bool scriptedCameraEnabled = false;

    RegisterSceneViewerSystems(harness.Schedule, camera, scriptedCameraEnabled);
    harness.Schedule.Init();

    SUCCEED() << "composition declared no cross-phase or unregistered edge";
}
