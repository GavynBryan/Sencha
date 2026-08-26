#include "SceneViewerSystems.h"

#include <app/GameContexts.h>
#include <input/InputActionResolveSystem.h>
#include <input/InputActionState.h>
#include <world/transform/TransformComponents.h>

#include <cmath>
#include <cstdint>

namespace
{
struct FreeCameraLookSystem
{
    explicit FreeCameraLookSystem(FreeCamera& camera)
        : Camera(camera)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        const auto* actions = ctx.Entities.TryGetResource<InputActionState>();
        if (actions == nullptr)
            return;
        Camera.UpdateLook(actions->Frame());
        Camera.ApplyRotation(ctx.Entities);
    }

    FreeCamera& Camera;
};

struct FreeCameraMovementSystem
{
    explicit FreeCameraMovementSystem(FreeCamera& camera)
        : Camera(camera)
    {
    }

    void FixedLogic(FixedLogicContext& ctx)
    {
        const auto* actions = ctx.Entities.TryGetResource<InputActionState>();
        if (actions == nullptr)
            return;
        Camera.TickFixed(
            actions->Tick(),
            ctx.Entities,
            static_cast<float>(ctx.Time.DeltaSeconds));
    }

    FreeCamera& Camera;
};

// Drives the active camera along a fixed orbit as a pure function of an
// internal frame counter, so every run of a given build renders the exact
// same view sequence. Runs in FrameUpdate (after the fixed-tick free-cam),
// so it wins for the presented frame whenever armed. Off by default; free
// fly-cam is untouched unless sceneviewer.camera.scripted is set.
//
// Keying the orbit to rendered frames rather than simulated time is
// deliberate and is the exception to the rule that presentation-rate systems
// leave simulation alone. Renderer A/B captures compare frame N of one build
// against frame N of another, which only means anything if frame N is the
// same view in both; a time-based orbit would move with whatever frame rate
// each build happened to achieve. It is a capture tool, not gameplay.
struct ScriptedCameraPathSystem
{
    ScriptedCameraPathSystem(FreeCamera& freeCamera, const bool& enabled)
        : FreeCam(freeCamera)
        , Enabled(enabled)
    {
    }

    void FrameUpdate(FrameUpdateContext& ctx)
    {
        if (!Enabled)
            return;
        LocalTransform* transform =
            ctx.Entities.TryGet<LocalTransform>(FreeCam.Entity);
        if (transform == nullptr)
            return;

        const double angle = static_cast<double>(FrameCounter) * kAngularStep;
        transform->Value.Position = Vec3d{
            static_cast<float>(std::cos(angle) * kRadius), kHeight,
            static_cast<float>(std::sin(angle) * kRadius) };
        const float yaw = static_cast<float>(angle) + kPi; // face the orbit center
        transform->Value.Rotation =
            Quatf::FromAxisAngle(Vec3d::Up(), yaw)
            * Quatf::FromAxisAngle(Vec3d::Right(), kPitch);
        ++FrameCounter;
    }

    FreeCamera& FreeCam;
    const bool& Enabled;
    std::uint64_t FrameCounter = 0;

    static constexpr double kAngularStep = 0.012;
    static constexpr double kRadius = 8.0;
    static constexpr double kHeight = 3.0;
    static constexpr float kPitch = -0.35f;
    static constexpr float kPi = 3.14159265358979323846f;
};
} // namespace

void RegisterSceneViewerSystems(
    EngineSchedule& schedule,
    FreeCamera& freeCamera,
    bool& scriptedCameraEnabled)
{
    schedule.Register<FreeCameraLookSystem>(freeCamera);
    schedule.Register<FreeCameraMovementSystem>(freeCamera);
    schedule.Register<ScriptedCameraPathSystem>(freeCamera, scriptedCameraEnabled);
    // After both registrations: the edge names its endpoints by type and
    // asserts on one that is not registered yet.
    //
    // Only the FixedLogic reader takes an edge. FreeCameraLookSystem is a
    // FrameUpdate system reading the presentation snapshot, and
    // FramePhase::Update already follows the ScheduleTicks phase that resolves
    // it -- After is phase-local, so an edge from it would record nothing and
    // assert. See input.md, "reading actions".
    schedule.After<FreeCameraMovementSystem, InputActionResolveSystem>();
}
