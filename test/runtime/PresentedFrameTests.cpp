#include <gtest/gtest.h>

#include <app/EngineSchedule.h>
#include <app/PresentationPhase.h>
#include <camera/CameraEffects.h>
#include <camera/CameraFovScale.h>
#include <camera/CameraShake.h>
#include <camera/PresentedFrame.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <ecs/StoragePartitionSet.h>
#include <ecs/World.h>
#include <render/CameraProjection.h>
#include <render/extract/Camera.h>
#include <time/FrameClock.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>
#include <world/transform/TransformPropagation.h>

#include <cmath>
#include <numbers>

namespace
{
constexpr RenderExtent kExtent{ 1280, 720 };

Transform3f At(Vec3d position, Quatf rotation = Quatf::Identity())
{
    return Transform3f(position, rotation, Vec3d::One());
}

PresentationTime Frame(std::uint64_t index, double alpha)
{
    PresentationTime time;
    time.FrameIndex = index;
    time.Alpha = alpha;
    return time;
}

void ExpectSame(const Transform3f& a, const Transform3f& b)
{
    EXPECT_EQ(a.Position, b.Position);
    EXPECT_EQ(a.Rotation.X, b.Rotation.X);
    EXPECT_EQ(a.Rotation.Y, b.Rotation.Y);
    EXPECT_EQ(a.Rotation.Z, b.Rotation.Z);
    EXPECT_EQ(a.Rotation.W, b.Rotation.W);
    EXPECT_EQ(a.Scale, b.Scale);
}

void ExpectSame(const Mat4& a, const Mat4& b)
{
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            EXPECT_EQ(a[row][column], b[row][column]) << row << "," << column;
}

struct Rig
{
    Rig()
    {
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<WorldTransform>();
        Entities.RegisterComponent<Parent>();
        Entities.RegisterComponent<WorldTransformHistory>();
        Entities.RegisterComponent<CameraComponent>();
        Visible.Add(StoragePartitionId::Default());

        Body = Entities.CreateEntity();
        Entities.AddComponent(Body, LocalTransform{ At(Vec3d(4.0f, 0.0f, 0.0f)) });
        Entities.AddComponent(Body, WorldTransform{});
        Entities.AddComponent(Body, WorldTransformHistory{
            .Previous = At(Vec3d(0.0f, 0.0f, 0.0f), Quatf::FromAxisAngle(Vec3d::Up(), 0.2f)),
            .Current = At(Vec3d(4.0f, 0.0f, 0.0f), Quatf::FromAxisAngle(Vec3d::Up(), 0.6f)),
            .Snap = false });

        Camera = Entities.CreateEntity();
        Entities.AddComponent(Camera, LocalTransform{
            At(Vec3d(0.0f, 1.6f, 0.0f), Quatf::FromAxisAngle(Vec3d::Right(), -0.3f)) });
        Entities.AddComponent(Camera, WorldTransform{});
        Entities.AddComponent(Camera, Parent{ Body });
        Entities.AddComponent(Camera, CameraComponent{});

        Held = Entities.CreateEntity();
        Entities.AddComponent(Held, LocalTransform{ At(Vec3d(0.3f, -0.2f, -0.5f)) });
        Entities.AddComponent(Held, WorldTransform{});
        Entities.AddComponent(Held, Parent{ Camera });

        Entities.AddResource<ActiveCameraService>().SetActive(Camera);
        Entities.AddResource<CameraShakeState>();
        Entities.AddResource<CameraFovScaleState>();

        PropagateTransforms(Entities, Visible, TransformPropagationDomain::Simulation);
    }

    void PropagateForPresentation(double alpha)
    {
        Entities.AdvanceFrame();
        PropagateTransforms(Entities, Visible, TransformPropagationDomain::Presentation, false, alpha);
    }

    void ShakeAndPulse()
    {
        CameraShakeProfile profile;
        profile.Noise.Rotation = Vec3d(2.0f, 1.0f, 1.0f);
        profile.Noise.Translation = Vec3d(0.02f, 0.02f, 0.0f);
        profile.Frequency = 15.0f;
        profile.Envelope = { .Attack = 0.05f, .Hold = 1.0f, .Release = 0.2f };
        const CameraEffectClock now{ .Tick = Clock.GetTickIndex(), .Alpha = 0.0, .TickSeconds = Clock.GetFixedDt() };
        Entities.GetResource<CameraShakeState>().Start(
            { .Timing = CameraEffectTiming::Once(profile.Envelope, Clock.GetTickIndex(), {}),
              .Profile = profile,
              .Scale = 1.0f,
              .Seed = 7,
              .Point = std::nullopt },
            now);
        CameraFovPulse pulse;
        pulse.Scale = 1.25f;
        pulse.Timing = CameraEffectTiming::Once({ .Attack = 0.05f, .Hold = 1.0f, .Release = 0.2f },
                                                Clock.GetTickIndex(), {});
        Entities.GetResource<CameraFovScaleState>().Start(pulse, now);
    }

    void Tick()
    {
        (void)Clock.BeginFixedTick();
        Clock.EndFixedTick();
    }

    World Entities;
    StoragePartitionSet Visible;
    FixedSimulationLoop Clock;
    EntityId Body;
    EntityId Camera;
    EntityId Held;
};

const ViewportRect kWindow{ .Offset = {}, .Size = { 1280.0f, 720.0f } };

const Mat4 kView = Mat4::Identity();
const Mat4 kPerspective = MakeVulkanPerspective(std::numbers::pi_v<float> / 2.0f, 1280.0f / 720.0f, 0.1f, 100.0f);
} // namespace

TEST(WorldPointProjection, APointStraightAheadLandsInTheMiddle)
{
    const ViewportProjection at = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(0.0f, 0.0f, -5.0f));
    EXPECT_EQ(at.Placement, ViewportPlacement::Visible);
    EXPECT_NEAR(at.Position.X, 640.0f, 1e-3f);
    EXPECT_NEAR(at.Position.Y, 360.0f, 1e-3f);
    EXPECT_NEAR(at.Normalized.X, 0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(at.ViewDepth, 5.0f);
    EXPECT_EQ(at.EdgeDirection, Vec2d());
}

TEST(WorldPointProjection, UpIsTheTopOfTheFrameAndRightIsRight)
{
    const ViewportProjection up = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(0.0f, 1.0f, -5.0f));
    const ViewportProjection right = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(1.0f, 0.0f, -5.0f));
    EXPECT_LT(up.Position.Y, 360.0f);
    EXPECT_GT(right.Position.X, 640.0f);
    EXPECT_NEAR(up.EdgeDirection.Y, -1.0f, 1e-5f);
    EXPECT_NEAR(right.EdgeDirection.X, 1.0f, 1e-5f);
}

TEST(WorldPointProjection, InFrontButOffTheFrameIsOutsideNotBehind)
{
    const ViewportProjection left = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(-20.0f, 0.0f, -5.0f));
    EXPECT_EQ(left.Placement, ViewportPlacement::OutsideViewport);
    EXPECT_LT(left.Position.X, 0.0f);
    EXPECT_NEAR(left.EdgeDirection.X, -1.0f, 1e-5f);

    const ViewportProjection beyondFar = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(0.0f, 0.0f, -500.0f));
    EXPECT_EQ(beyondFar.Placement, ViewportPlacement::OutsideViewport);
    const ViewportProjection insideNear = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(0.0f, 0.0f, -0.05f));
    EXPECT_EQ(insideNear.Placement, ViewportPlacement::OutsideViewport);
}

TEST(WorldPointProjection, BehindTheCameraPointsTheWayToTurn)
{
    // Mirrored to the left by the projection; the way to turn is right.
    const ViewportProjection behind = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(2.0f, 0.0f, 5.0f));
    EXPECT_EQ(behind.Placement, ViewportPlacement::BehindCamera);
    EXPECT_LE(behind.ViewDepth, 0.0f);
    EXPECT_NEAR(behind.EdgeDirection.X, 1.0f, 1e-5f);
    EXPECT_EQ(behind.Position, Vec2d());

    const ViewportProjection onThePlane = ProjectWorldPointToViewport(kView, kPerspective, kWindow, Vec3d(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(onThePlane.Placement, ViewportPlacement::BehindCamera);
}

TEST(WorldPointProjection, ARectangleWithAnOffsetIsWhereATileSits)
{
    const ViewportRect tile{ .Offset = { 100.0f, 50.0f }, .Size = { 640.0f, 360.0f } };
    const ViewportProjection at = ProjectWorldPointToViewport(kView, kPerspective, tile, Vec3d(0.0f, 0.0f, -5.0f));
    EXPECT_NEAR(at.Position.X, 420.0f, 1e-3f);
    EXPECT_NEAR(at.Position.Y, 230.0f, 1e-3f);
}

TEST(WorldPointProjection, OrthographicViewsProjectWithoutPerspective)
{
    const Mat4 ortho = MakeVulkanOrthographic(-8.0f, 8.0f, -4.5f, 4.5f, 0.1f, 100.0f);
    const ViewportProjection near = ProjectWorldPointToViewport(kView, ortho, kWindow, Vec3d(4.0f, 0.0f, -2.0f));
    const ViewportProjection far = ProjectWorldPointToViewport(kView, ortho, kWindow, Vec3d(4.0f, 0.0f, -50.0f));
    EXPECT_EQ(near.Placement, ViewportPlacement::Visible);
    EXPECT_NEAR(near.Position.X, 960.0f, 1e-3f);
    EXPECT_NEAR(near.Position.X, far.Position.X, 1e-3f);
    EXPECT_EQ(ProjectWorldPointToViewport(kView, ortho, kWindow, Vec3d(0.0f, 0.0f, 3.0f)).Placement,
              ViewportPlacement::BehindCamera);
}

TEST(PresentationPose, IsWherePresentationPropagationWillPutEveryLevelOfARig)
{
    Rig rig;
    constexpr double kAlpha = 0.375;
    const Transform3f body = PresentationPoseOf(rig.Entities, rig.Body, kAlpha);
    const Transform3f camera = PresentationPoseOf(rig.Entities, rig.Camera, kAlpha);
    const Transform3f held = PresentationPoseOf(rig.Entities, rig.Held, kAlpha);

    rig.PropagateForPresentation(kAlpha);
    ExpectSame(body, ResolvePresentationPose(*rig.Entities.TryGet<WorldTransformHistory>(rig.Body), kAlpha));
    ExpectSame(camera, rig.Entities.TryGet<WorldTransform>(rig.Camera)->Value);
    ExpectSame(held, rig.Entities.TryGet<WorldTransform>(rig.Held)->Value);
    ExpectSame(PresentationPoseOf(rig.Entities, rig.Held, kAlpha), held);
}

TEST(PresentationPose, AChainWithNoHistoryIsItsLocalTransformsComposed)
{
    Rig rig;
    rig.Entities.RemoveComponent<WorldTransformHistory>(rig.Body);
    rig.Entities.TryGet<LocalTransform>(rig.Body)->Value = At(Vec3d(-2.0f, 0.5f, 7.0f));
    const Transform3f held = PresentationPoseOf(rig.Entities, rig.Held, 0.5);
    rig.PropagateForPresentation(0.5);
    ExpectSame(held, rig.Entities.TryGet<WorldTransform>(rig.Held)->Value);
}

TEST(PresentedFrame, ResolvingWritesNothingASimulationReaderSees)
{
    Rig rig;
    rig.ShakeAndPulse();
    const Transform3f cameraTick = rig.Entities.TryGet<WorldTransform>(rig.Camera)->Value;
    const Transform3f heldTick = rig.Entities.TryGet<WorldTransform>(rig.Held)->Value;
    const WorldTransformHistory history = *rig.Entities.TryGet<WorldTransformHistory>(rig.Body);

    const PresentedFrame& frame = PublishPresentedFrame(rig.Entities, rig.Clock, Frame(3, 0.5), kExtent, rig.Visible);
    ASSERT_TRUE(frame.Valid);

    ExpectSame(rig.Entities.TryGet<WorldTransform>(rig.Camera)->Value, cameraTick);
    ExpectSame(rig.Entities.TryGet<WorldTransform>(rig.Held)->Value, heldTick);
    ExpectSame(rig.Entities.TryGet<WorldTransformHistory>(rig.Body)->Previous, history.Previous);
    EXPECT_NE(frame.Placed.Position, cameraTick.Position);
}

TEST(PresentedFrame, ExtractionDrawsTheMatricesOverlaysLaidOutAgainst)
{
    Rig rig;
    rig.ShakeAndPulse();
    rig.Tick();
    rig.Tick();
    const PresentationTime now = Frame(9, 0.625);
    const PresentedFrame& frame = PublishPresentedFrame(rig.Entities, rig.Clock, now, kExtent, rig.Visible);
    ASSERT_TRUE(frame.Valid);
    EXPECT_NE(rig.Entities.GetResource<CameraShakeState>().OffsetFor(rig.Camera).Rotation, Vec3d());
    EXPECT_GT(rig.Entities.GetResource<CameraFovScaleState>().ScaleFor(rig.Camera), 1.0f);

    rig.PropagateForPresentation(now.Alpha);
    const PresentedFrame& drawn = EnsurePresentedFrame(rig.Entities, rig.Clock, now, kExtent, rig.Visible);
    EXPECT_EQ(&drawn, &frame);
    CameraRenderData camera;
    ASSERT_TRUE(CameraRenderDataSystem::Build(drawn, rig.Entities, camera));
    ExpectSame(camera.ViewProjection, frame.ViewProjection);

    CameraRenderData before;
    ASSERT_TRUE(CameraRenderDataSystem::Build(rig.Entities.GetResource<ActiveCameraService>(), rig.Entities, kExtent,
                                              before, now.Alpha));
    ExpectSame(before.ViewProjection, frame.ViewProjection);

    const Vec3d point = rig.Entities.TryGet<WorldTransform>(rig.Held)->Value.Position;
    const Vec4 clip = camera.ViewProjection * Vec4{ point.X, point.Y, point.Z, 1.0f };
    const ViewportProjection at = ProjectWorldPoint(frame, kWindow, point);
    EXPECT_NEAR(at.Position.X, (clip.X / clip.W + 1.0f) * 0.5f * 1280.0f, 1e-2f);
    EXPECT_NEAR(at.Position.Y, (clip.Y / clip.W + 1.0f) * 0.5f * 720.0f, 1e-2f);
}

TEST(PresentedFrame, AShakenViewMovesWhatIsLaidOverIt)
{
    Rig rig;
    const Vec3d marker = PresentationPoseOf(rig.Entities, rig.Camera, 0.5).Position
                       + PresentationPoseOf(rig.Entities, rig.Camera, 0.5).Rotation.RotateVector(Vec3d(0.0f, 0.0f, -4.0f));
    const Vec2d still = ProjectWorldPoint(PublishPresentedFrame(rig.Entities, rig.Clock, Frame(1, 0.5), kExtent, rig.Visible),
                                          kWindow, marker).Position;
    EXPECT_NEAR(still.X, 640.0f, 1e-2f);
    EXPECT_NEAR(still.Y, 360.0f, 1e-2f);

    // Past the shake's 50 ms attack.
    rig.ShakeAndPulse();
    for (int tick = 0; tick < 6; ++tick)
        rig.Tick();
    const Vec2d shaken = ProjectWorldPoint(PublishPresentedFrame(rig.Entities, rig.Clock, Frame(2, 0.5), kExtent, rig.Visible),
                                           kWindow, marker).Position;
    EXPECT_GT((shaken - still).Magnitude(), 2.0f);
}

TEST(PresentedFrame, ExtractionPublishesAgainForAnotherFrameOrAnotherCamera)
{
    Rig rig;
    const PresentedFrame first = PublishPresentedFrame(rig.Entities, rig.Clock, Frame(1, 0.25), kExtent, rig.Visible);

    const PresentedFrame& next = EnsurePresentedFrame(rig.Entities, rig.Clock, Frame(2, 0.75), kExtent, rig.Visible);
    EXPECT_EQ(next.FrameIndex, 2u);
    EXPECT_NE(next.Placed.Position, first.Placed.Position);

    EXPECT_EQ(EnsurePresentedFrame(rig.Entities, rig.Clock, Frame(2, 0.75), { 640, 480 }, rig.Visible).Extent,
              (RenderExtent{ 640, 480 }));

    const EntityId other = rig.Entities.CreateEntity();
    rig.Entities.AddComponent(other, LocalTransform{ At(Vec3d(0.0f, 10.0f, 0.0f)) });
    rig.Entities.AddComponent(other, WorldTransform{});
    rig.Entities.AddComponent(other, CameraComponent{});
    rig.Entities.GetResource<ActiveCameraService>().SetActive(other);
    const PresentedFrame& switched = EnsurePresentedFrame(rig.Entities, rig.Clock, Frame(2, 0.75), { 640, 480 }, rig.Visible);
    EXPECT_EQ(switched.Camera, other);
    EXPECT_EQ(switched.Placed.Position, Vec3d(0.0f, 10.0f, 0.0f));
}

TEST(PresentedFrame, NothingToDrawIsAnInvalidFrame)
{
    Rig rig;
    EXPECT_FALSE(PublishPresentedFrame(rig.Entities, rig.Clock, Frame(1, 0.0), {}, rig.Visible).Valid);

    StoragePartitionSet elsewhere;
    EXPECT_FALSE(PublishPresentedFrame(rig.Entities, rig.Clock, Frame(2, 0.0), kExtent, elsewhere).Valid);

    rig.Entities.GetResource<ActiveCameraService>().SetActive({});
    const PresentedFrame& none = PublishPresentedFrame(rig.Entities, rig.Clock, Frame(3, 0.0), kExtent, rig.Visible);
    EXPECT_FALSE(none.Valid);
    CameraRenderData camera;
    EXPECT_FALSE(CameraRenderDataSystem::Build(none, rig.Entities, camera));
    EXPECT_EQ(ProjectWorldPoint(none, kWindow, Vec3d(0.0f, 0.0f, -1.0f)).Placement, ViewportPlacement::BehindCamera);
}

namespace
{
struct Overlay
{
    const PresentedFrame* Seen = nullptr;
    PresentedFrame Copy;
    int Runs = 0;
    void PresentationUpdate(PresentationUpdateContext& ctx)
    {
        Seen = &ctx.Frame;
        Copy = ctx.Frame;
        ++Runs;
    }
};

struct LaterOverlay
{
    Overlay* First = nullptr;
    bool SawFirst = false;
    void PresentationUpdate(PresentationUpdateContext&) { SawFirst = First->Runs > 0; }
};

struct Mover
{
    int Runs = 0;
    void FrameUpdate(FrameUpdateContext&) { ++Runs; }
};
} // namespace

TEST(PresentationPhase, OverlaysSeeThisFramesViewAndExtractionReusesIt)
{
    EngineSchedule schedule;
    Mover& mover = schedule.Register<Mover>();
    LaterOverlay& later = schedule.Register<LaterOverlay>();
    Overlay& overlay = schedule.Register<Overlay>();
    later.First = &overlay;
    schedule.After<LaterOverlay, Overlay>();
    schedule.Init();

    Rig rig;
    EngineConfig config;
    RuntimeFrameLoop runtime;
    // Presentation time is built after the ticks, as FrameDriver orders a frame.
    for (int frame = 0; frame < 3; ++frame)
    {
        (void)runtime.BeginFrame();
        (void)runtime.BuildPresentationFrame();
    }
    const PresentationTime now = runtime.GetCurrentFrame().Presentation;
    ASSERT_GT(now.FrameIndex, 0u);

    const PresentedFrame& published =
        RunPresentationPhase(schedule, config, runtime, rig.Entities, rig.Visible, kExtent);

    EXPECT_EQ(overlay.Runs, 1);
    EXPECT_EQ(overlay.Seen, &published);
    EXPECT_EQ(overlay.Seen, TryPresentedFrame(rig.Entities));
    EXPECT_TRUE(overlay.Copy.Valid);
    EXPECT_EQ(overlay.Copy.FrameIndex, now.FrameIndex);
    EXPECT_TRUE(later.SawFirst);
    EXPECT_EQ(mover.Runs, 0);

    const std::uint64_t publication = published.Publication;
    rig.PropagateForPresentation(now.Alpha);
    const PresentedFrame& drawn =
        EnsurePresentedFrame(rig.Entities, runtime.GetSimulationClock(), now, kExtent, rig.Visible);
    EXPECT_EQ(drawn.Publication, publication);
    ExpectSame(drawn.ViewProjection, overlay.Copy.ViewProjection);

    (void)runtime.BeginFrame();
    (void)runtime.BuildPresentationFrame();
    (void)RunPresentationPhase(schedule, config, runtime, rig.Entities, rig.Visible, kExtent);
    EXPECT_EQ(overlay.Runs, 2);
    EXPECT_EQ(overlay.Copy.Publication, publication + 1);
    EXPECT_EQ(overlay.Copy.FrameIndex, runtime.GetCurrentFrame().Presentation.FrameIndex);
}

TEST(PresentationPhase, AHostThatDrawsNothingPublishesAnInvalidFrameAndPresentsNoEffects)
{
    EngineSchedule schedule;
    Overlay& overlay = schedule.Register<Overlay>();
    schedule.Init();

    Rig rig;
    rig.ShakeAndPulse();
    for (int tick = 0; tick < 6; ++tick)
        rig.Tick();
    EngineConfig config;
    RuntimeFrameLoop runtime;
    (void)runtime.BeginFrame();
    (void)runtime.BuildPresentationFrame();
    (void)RunPresentationPhase(schedule, config, runtime, rig.Entities, rig.Visible, RenderExtent{});
    EXPECT_EQ(overlay.Runs, 1);
    EXPECT_FALSE(overlay.Copy.Valid);
    EXPECT_EQ(rig.Entities.GetResource<CameraShakeState>().OffsetFor(rig.Camera).Rotation, Vec3d());
}
