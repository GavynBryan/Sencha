#include <gtest/gtest.h>

#include <assets/data/DataAssetCache.h>
#include <authored/AuthoredValue.h>
#include <authored/VerbDispatcher.h>
#include <camera/CameraExclusion.h>
#include <camera/CameraFovScale.h>
#include <camera/CameraFovScaleVerbs.h>
#include <camera/CameraShake.h>
#include <camera/CameraShakeVerbs.h>
#include <camera/CameraSubject.h>
#include <camera/PresentedView.h>
#include <components/ActiveCameraService.h>
#include <components/CameraComponent.h>
#include <core/json/JsonParser.h>
#include <ecs/World.h>
#include <render/CameraProjection.h>
#include <render/extract/Camera.h>
#include <time/SimClock.h>
#include <world/transform/TransformComponents.h>
#include <world/transform/TransformHistory.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

namespace
{
constexpr double kTick = 1.0 / 60.0;
constexpr const char* kProfilePath = "asset://shakes/test.sdata";

CameraEffectClock At(std::uint64_t tick, double alpha = 0.0)
{
    return { .Tick = tick, .Alpha = alpha, .TickSeconds = kTick };
}

bool Compile(std::string_view json, CameraShakeProfile& out, std::string& error)
{
    const std::optional<JsonValue> data = JsonParse(json);
    return data.has_value() && CompileCameraShakeProfile(*data, out, error);
}

VerbInvocation Call(std::uint64_t tick, const AuthoredArguments* arguments, LeaseOwner owner = {},
                    LeaseStep step = LeaseStep::None)
{
    VerbInvocation call;
    call.Tick = tick;
    call.Owner = owner;
    call.Step = step;
    call.Arguments = arguments;
    return call;
}

CameraShakeProfile Noisy()
{
    CameraShakeProfile profile;
    profile.Noise.Rotation = Vec3d(2.0f, 1.0f, 1.0f);
    profile.Noise.Translation = Vec3d(0.02f, 0.02f, 0.0f);
    profile.Kick.Translation = Vec3d(0.0f, -0.05f, 0.0f);
    profile.Frequency = 15.0f;
    profile.Envelope = { .Attack = 0.05f, .Hold = 0.1f, .Release = 0.2f };
    return profile;
}

CameraShakeInstance OnceAt(std::uint64_t tick, EntityId subject = {}, CameraShakeProfile profile = Noisy())
{
    return { .Timing = CameraEffectTiming::Once(profile.Envelope, tick, subject),
             .Profile = profile,
             .Scale = 1.0f,
             .Seed = 7,
             .Point = std::nullopt };
}

// A camera that presents a pawn's view, as a first-person rig does, and one
// that presents somebody else's.
struct Views
{
    Views()
    {
        Entities.RegisterComponent<CameraComponent>();
        Entities.RegisterComponent<CameraExclusion>();
        Entities.RegisterComponent<CameraSubject>();
        Entities.RegisterComponent<LocalTransform>();
        Entities.RegisterComponent<WorldTransform>();
        Entities.RegisterComponent<WorldTransformHistory>();
        Pawn = Entities.CreateEntity();
        Remote = Entities.CreateEntity();
        Camera = Entities.CreateEntity();
        Entities.AddComponent<CameraComponent>(Camera, {});
        Entities.AddComponent<WorldTransform>(Camera, {});
        SetCameraSubject(Entities, Camera, Pawn);
        Entities.AddResource<ActiveCameraService>().SetActive(Camera);
        Entities.AddResource<CameraShakeState>();
        Entities.AddResource<CameraFovScaleState>();
    }

    CameraShakeState& Shake() { return Entities.GetResource<CameraShakeState>(); }
    CameraFovScaleState& Fov() { return Entities.GetResource<CameraFovScaleState>(); }

    CameraShakeOffset ShakeAt(const CameraEffectClock& now)
    {
        Shake().Update(Entities, Camera, Vec3d::Zero(), now);
        return Shake().OffsetFor(Camera);
    }

    float FovAt(const CameraEffectClock& now)
    {
        Fov().Update(Entities, Camera, now);
        return Fov().ScaleFor(Camera);
    }

    bool Build(CameraRenderData& out)
    {
        return CameraRenderDataSystem::Build(Entities.GetResource<ActiveCameraService>(), Entities,
                                             RenderExtent{ 1280, 720 }, out);
    }

    World Entities;
    EntityId Pawn;
    EntityId Remote;
    EntityId Camera;
};
} // namespace

TEST(CameraShakeProfile, ReadsEveryChannelAndTheEnvelope)
{
    CameraShakeProfile profile;
    std::string error;
    ASSERT_TRUE(Compile(R"({
        "noise": { "rotation": [1, 2, 3], "translation": [0.1, 0.2, 0.3] },
        "kick": { "translation": [0, -0.05, 0] },
        "frequency": 12, "attack": 0.01, "hold": 0.02, "release": 0.3,
        "falloff": { "inner": 2, "outer": 10 }
    })", profile, error)) << error;
    EXPECT_EQ(profile.Noise.Rotation, Vec3d(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(profile.Kick.Translation, Vec3d(0.0f, -0.05f, 0.0f));
    EXPECT_FLOAT_EQ(profile.Frequency, 12.0f);
    EXPECT_FLOAT_EQ(profile.Envelope.Release, 0.3f);
    ASSERT_TRUE(profile.Falloff.has_value());
    EXPECT_FLOAT_EQ(profile.Falloff->Outer, 10.0f);
}

TEST(CameraShakeProfile, RefusesWhatCannotBePresented)
{
    const char* invalid[] = {
        R"({ "attack": -0.1 })",
        R"({ "release": "soon" })",
        R"({ "kick": { "rotation": [1, 2] } })",
        R"({ "noise": { "rotation": [1, 0, 0] } })",
        R"({ "noise": { "rotation": [1, 0, 0] }, "frequency": -3 })",
        R"({ "noise": { "rotation": [1, 0, 0] }, "frequency": 500 })",
        R"({ "falloff": { "inner": 5, "outer": 5 } })",
        R"({ "falloff": { "inner": -1, "outer": 5 } })",
        R"({ "falloff": { "inner": 1 } })",
    };
    for (const char* json : invalid)
    {
        CameraShakeProfile profile;
        std::string error;
        EXPECT_FALSE(Compile(json, profile, error)) << json;
        EXPECT_FALSE(error.empty()) << json;
    }
    // JSON has no infinity, so the finite check is the compiler's own.
    JsonValue data = *JsonParse(R"({ "hold": 1 })");
    for (auto& [key, value] : data.AsObject())
        if (key == "hold")
            value = JsonValue(std::numeric_limits<double>::infinity());
    CameraShakeProfile profile;
    std::string error;
    EXPECT_FALSE(CompileCameraShakeProfile(data, profile, error));
}

TEST(CameraEffectTiming, RisesHoldsAndReleasesOnSmoothRamps)
{
    const CameraEffectTiming timing = CameraEffectTiming::Once({ .Attack = 0.1f, .Hold = 0.2f, .Release = 0.4f }, 10, {});
    EXPECT_FLOAT_EQ(timing.Level(0.0), 0.0f);
    EXPECT_FLOAT_EQ(timing.Level(0.05), 0.5f);
    EXPECT_FLOAT_EQ(timing.Level(0.2), 1.0f);
    EXPECT_FLOAT_EQ(timing.Level(0.5), 0.5f);
    EXPECT_FALSE(timing.Expired(0.69));
    EXPECT_TRUE(timing.Expired(0.7001));
    EXPECT_NEAR(timing.Level(0.7), 0.0f, 1e-6f);
    EXPECT_DOUBLE_EQ(timing.Elapsed(At(11)), 0.0) << "started by tick 10's presentation";
    EXPECT_NEAR(timing.Elapsed(At(13, 0.5)), 2.5 * kTick, 1e-12);
}

TEST(CameraEffectTiming, AHeldEffectStaysUntilLetGoThenReleasesFromWhereItWas)
{
    CameraEffectTiming timing = CameraEffectTiming::Held({ .Attack = 1.0f, .Hold = 0.0f, .Release = 0.5f }, 0, {},
                                                         LeaseOwner{ EntityId{}, 1 });
    EXPECT_FLOAT_EQ(timing.Level(100.0), 1.0f);
    EXPECT_FALSE(timing.Expired(100.0));
    timing.ReleaseAfter(30, 1.0 / 60.0);
    EXPECT_NEAR(timing.ReleaseAt, 0.5, 1e-9);
    EXPECT_FLOAT_EQ(timing.Level(0.5), 0.5f) << "let go halfway up, it falls from halfway";
    EXPECT_FLOAT_EQ(timing.Level(0.75), 0.25f);
    EXPECT_TRUE(timing.Expired(1.0));
}

TEST(CameraShake, NoiseIsAFunctionOfTimeAndSeedAlone)
{
    const CameraShakeProfile profile = Noisy();
    const CameraShakeOffset a = EvaluateCameraShake(profile, 3, 0.137, 1.0f);
    EXPECT_EQ(a.Rotation, EvaluateCameraShake(profile, 3, 0.137, 1.0f).Rotation);
    EXPECT_NE(a.Rotation, EvaluateCameraShake(profile, 4, 0.137, 1.0f).Rotation);
    EXPECT_NE(a.Rotation, EvaluateCameraShake(profile, 3, 0.2, 1.0f).Rotation);
    for (float t = 0.0f; t < 2.0f; t += 0.01f)
    {
        const CameraShakeOffset offset = EvaluateCameraShake(profile, 3, t, 1.0f);
        EXPECT_LE(std::abs(offset.Rotation.X), profile.Noise.Rotation.X + 1e-5f);
        EXPECT_LE(std::abs(offset.Translation.Y - profile.Kick.Translation.Y),
                  profile.Noise.Translation.Y + 1e-5f);
    }
}

// Frames land wherever they land between ticks; the same instant of
// presentation time is the same shake however many frames led up to it.
TEST(CameraShake, TheSameInstantShakesTheSameAtAnyFrameRate)
{
    Views fast;
    Views slow;
    fast.Shake().Start(OnceAt(100), At(101));
    slow.Shake().Start(OnceAt(100), At(101));
    for (int frame = 0; frame < 30; ++frame)
        (void)fast.ShakeAt(At(101 + frame / 4, (frame % 4) * 0.25));
    const CameraShakeOffset fastOffset = fast.ShakeAt(At(108, 0.5));
    const CameraShakeOffset slowOffset = slow.ShakeAt(At(108, 0.5));
    EXPECT_EQ(fastOffset.Rotation, slowOffset.Rotation);
    EXPECT_EQ(fastOffset.Translation, slowOffset.Translation);
}

TEST(CameraShake, ASuspendedClockHoldsTheShakeWhereItIsAndResumesIt)
{
    Views views;
    views.Shake().Start(OnceAt(100), At(101));
    const CameraShakeOffset paused = views.ShakeAt(At(105, 0.3));
    for (int frame = 0; frame < 120; ++frame)
        EXPECT_EQ(views.ShakeAt(At(105, 0.3)).Rotation, paused.Rotation) << "no tick, no alpha: no time";
    EXPECT_EQ(views.Shake().Count(), 1u);
    EXPECT_NE(views.ShakeAt(At(106, 0.3)).Rotation, paused.Rotation);
    (void)views.ShakeAt(At(200));
    EXPECT_EQ(views.Shake().Count(), 0u) << "resumed, it runs out as it would have";
}

TEST(CameraShake, ZeroScaleIsExactlyTheCamerasOwnViewEvenMidShake)
{
    Views views;
    views.Shake().Start(OnceAt(100), At(101));
    CameraRenderData shaken;
    (void)views.ShakeAt(At(105, 0.5));
    ASSERT_TRUE(views.Build(shaken));
    views.Shake().Settings.Scale = 0.0f;
    EXPECT_TRUE(views.ShakeAt(At(105, 0.5)).IsIdentity());
    CameraRenderData still;
    ASSERT_TRUE(views.Build(still));
    EXPECT_EQ(still.View, Mat4::Identity().AffineInverse());
    EXPECT_NE(shaken.View, still.View);
    views.Shake().Settings.Scale = 1.0f;
    EXPECT_FALSE(views.ShakeAt(At(105, 0.5)).IsIdentity()) << "turned back on, the shake is still running";
}

TEST(CameraShake, OnlyTheViewItsSubjectOwnsShakes)
{
    Views views;
    views.Shake().Start(OnceAt(100, views.Remote), At(101));
    EXPECT_TRUE(views.ShakeAt(At(105)).IsIdentity()) << "another player's landing is not this view's";
    views.Shake().Start(OnceAt(100, views.Pawn), At(101));
    EXPECT_FALSE(views.ShakeAt(At(105)).IsIdentity());
    views.Shake().Clear();
    views.Shake().Start(OnceAt(100), At(101));
    EXPECT_FALSE(views.ShakeAt(At(105)).IsIdentity()) << "a shake for nobody in particular is everyone's";

    views.Shake().Clear();
    views.Shake().Start(OnceAt(100, views.Pawn), At(101));
    SetCameraSubject(views.Entities, views.Camera, views.Remote);
    EXPECT_TRUE(views.ShakeAt(At(105)).IsIdentity()) << "the view changed hands: spectating is not landing";
}

TEST(CameraShake, AShakeFromAPointWeakensWithDistance)
{
    Views views;
    CameraShakeProfile profile = Noisy();
    profile.Falloff = CameraShakeFalloff{ .Inner = 2.0f, .Outer = 10.0f };
    CameraShakeInstance shake = OnceAt(100, {}, profile);
    shake.Point = Vec3d(0.0f, 0.0f, 0.0f);
    views.Shake().Start(shake, At(101));
    const auto at = [&](float distance) {
        views.Shake().Update(views.Entities, views.Camera, Vec3d(distance, 0.0f, 0.0f), At(110));
        return views.Shake().OffsetFor(views.Camera).Translation.Y;
    };
    EXPECT_FLOAT_EQ(at(6.0f), at(1.0f) * 0.5f);
    EXPECT_FLOAT_EQ(at(12.0f), 0.0f);
}

TEST(CameraShake, ManyShakesTogetherStayInsideTheLimits)
{
    Views views;
    CameraShakeProfile profile;
    profile.Kick.Rotation = Vec3d(5.0f, 0.0f, 0.0f);
    profile.Envelope.Hold = 1.0f;
    for (int i = 0; i < 20; ++i)
        views.Shake().Start(OnceAt(100, {}, profile), At(101));
    EXPECT_EQ(views.Shake().Count(), CameraShakeState::kCapacity);
    EXPECT_FLOAT_EQ(views.ShakeAt(At(110)).Rotation.X, CameraShakeSettings{}.MaxAngle);
}

TEST(CameraShake, AHoldLetGoOrWhoseHolderIsGoneSettles)
{
    Views views;
    CameraShakeProfile profile;
    profile.Kick.Translation = Vec3d(0.0f, 0.1f, 0.0f);
    profile.Envelope = { .Attack = 0.0f, .Hold = 0.0f, .Release = 0.1f };
    const LeaseOwner holder{ views.Pawn, 9 };
    views.Shake().Start({ .Timing = CameraEffectTiming::Held(profile.Envelope, 100, {}, holder), .Profile = profile, .Scale = 1.0f, .Seed = 0,
                          .Point = std::nullopt },
                        At(101));
    EXPECT_FLOAT_EQ(views.ShakeAt(At(400)).Translation.Y, 0.1f) << "held, it does not end";
    views.Shake().Release(holder, 400, kTick);
    EXPECT_GT(views.ShakeAt(At(402)).Translation.Y, 0.0f) << "let go, it eases out";
    (void)views.ShakeAt(At(410));
    EXPECT_EQ(views.Shake().Count(), 0u);

    views.Shake().Start({ .Timing = CameraEffectTiming::Held(profile.Envelope, 500, {}, holder), .Profile = profile, .Scale = 1.0f, .Seed = 0,
                          .Point = std::nullopt },
                        At(501));
    views.Entities.DestroyEntity(views.Pawn);
    (void)views.ShakeAt(At(502));
    (void)views.ShakeAt(At(520));
    EXPECT_EQ(views.Shake().Count(), 0u) << "a holder that is gone lets go";
}

// The view moves; the camera, and everything that aims from it, does not.
TEST(CameraShake, TheRenderedViewMovesWhileTheCameraStaysPut)
{
    Views views;
    Transform3f placed;
    placed.Position = Vec3d(1.0f, 2.0f, 3.0f);
    views.Entities.TryGet<WorldTransform>(views.Camera)->Value = placed;
    CameraShakeProfile profile;
    profile.Kick.Rotation = Vec3d(2.0f, 0.0f, 0.0f);
    profile.Kick.Translation = Vec3d(0.0f, -0.1f, 0.0f);
    profile.Envelope.Hold = 1.0f;
    views.Shake().Start(OnceAt(100, {}, profile), At(101));
    (void)views.ShakeAt(At(110));

    CameraRenderData data;
    ASSERT_TRUE(views.Build(data));
    EXPECT_EQ(views.Entities.TryGet<WorldTransform>(views.Camera)->Value.Position, placed.Position);
    EXPECT_NEAR(data.Position.Y, 1.9f, 1e-5f);
    EXPECT_NE(data.View, placed.ToMat4().AffineInverse());
    const Frustum expected = Frustum::FromViewProjection(data.Projection * data.View);
    for (std::size_t plane = 0; plane < 6; ++plane)
        EXPECT_EQ(data.ViewFrustum.Planes[plane].Normal, expected.Planes[plane].Normal)
            << "culling uses the view that is drawn";

    const EntityId other = views.Entities.CreateEntity();
    views.Entities.AddComponent<CameraComponent>(other, {});
    views.Entities.AddComponent<WorldTransform>(other, WorldTransform{ placed });
    views.Entities.GetResource<ActiveCameraService>().SetActive(other);
    ASSERT_TRUE(views.Build(data));
    EXPECT_EQ(data.View, placed.ToMat4().AffineInverse()) << "summed for one camera, it moves no other";
}

// What is laid over a frame reads the view the frame is drawn with: the same
// shaken pose and scaled field of view, so a direction lands where it is drawn.
TEST(PresentedView, IsTheViewTheFrameIsDrawnWith)
{
    Views views;
    Transform3f placed;
    placed.Position = Vec3d(1.0f, 2.0f, 3.0f);
    views.Entities.TryGet<WorldTransform>(views.Camera)->Value = placed;
    const CameraComponent& camera = *views.Entities.TryGet<CameraComponent>(views.Camera);
    PresentedView still = PresentCameraView(views.Entities, views.Camera, camera, placed);
    EXPECT_EQ(still.Pose.ToMat4(), placed.ToMat4());
    EXPECT_FLOAT_EQ(still.FovYRadians, camera.FovYRadians);

    CameraShakeProfile profile;
    profile.Kick.Rotation = Vec3d(2.0f, 0.0f, 0.0f);
    profile.Envelope.Hold = 1.0f;
    views.Shake().Start(OnceAt(100, {}, profile), At(101));
    (void)views.ShakeAt(At(110));
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Hold = 1.0f }, 100, {}), .Scale = 0.5f }, At(101));
    (void)views.FovAt(At(110));

    CameraRenderData data;
    ASSERT_TRUE(views.Build(data));
    const PresentedView presented = PresentCameraView(views.Entities, views.Camera, camera, placed);
    EXPECT_EQ(data.View, presented.Pose.ToMat4().AffineInverse());
    EXPECT_FLOAT_EQ(presented.FovYRadians, camera.FovYRadians * 0.5f);
    EXPECT_NEAR(data.Projection[1][1], -1.0f / std::tan(presented.FovYRadians * 0.5f), 1e-5f);

    // The camera's own forward, where aiming points, through the drawn view:
    // a view kicked up sees it below the centre, exactly where the frame's own
    // projection of a far point along it lands.
    const Vec3d aim = placed.Rotation.RotateVector(Vec3d::Forward());
    const std::optional<Vec2d> lands =
        ProjectDirectionToViewport(presented.Pose.Rotation, presented.FovYRadians, 1280.0f, 720.0f, aim);
    ASSERT_TRUE(lands.has_value());
    EXPECT_NEAR(lands->X, 640.0f, 1e-3f);
    EXPECT_GT(lands->Y, 360.0f + 1.0f);
    const Vec3d far = placed.Position + aim * 10000.0f;
    const Vec4 clip = data.ViewProjection * Vec4{ far.X, far.Y, far.Z, 1.0f };
    EXPECT_NEAR(lands->X, (clip.X / clip.W + 1.0f) * 0.5f * 1280.0f, 0.05f);
    EXPECT_NEAR(lands->Y, (clip.Y / clip.W + 1.0f) * 0.5f * 720.0f, 0.05f);
}

TEST(PresentedView, ADirectionBehindTheViewLandsNowhere)
{
    const float fov = ToRadians(90.0f);
    const std::optional<Vec2d> ahead = ProjectDirectionToViewport(Quatf::Identity(), fov, 200.0f, 100.0f, Vec3d::Forward());
    ASSERT_TRUE(ahead.has_value());
    EXPECT_NEAR(ahead->X, 100.0f, 1e-4f);
    EXPECT_NEAR(ahead->Y, 50.0f, 1e-4f);
    const std::optional<Vec2d> top =
        ProjectDirectionToViewport(Quatf::Identity(), fov, 200.0f, 100.0f, (Vec3d::Forward() + Vec3d::Up()).Normalized());
    ASSERT_TRUE(top.has_value());
    EXPECT_NEAR(top->Y, 0.0f, 1e-3f) << "half the field of view up is the top edge";
    EXPECT_FALSE(ProjectDirectionToViewport(Quatf::Identity(), fov, 200.0f, 100.0f, Vec3d::Backward()).has_value());
    EXPECT_FALSE(ProjectDirectionToViewport(Quatf::Identity(), fov, 200.0f, 100.0f, Vec3d::Up()).has_value());
}

TEST(CameraFovScale, PulsesMultiplyTheCamerasOwnFieldOfView)
{
    Views views;
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Hold = 1.0f }, 100, views.Pawn), .Scale = 1.2f }, At(101));
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Hold = 1.0f }, 100, {}), .Scale = 0.5f }, At(101));
    EXPECT_FLOAT_EQ(views.FovAt(At(110)), 0.6f);

    views.Entities.TryGet<CameraComponent>(views.Camera)->FovYRadians = ToRadians(90.0f);
    CameraRenderData narrowed;
    ASSERT_TRUE(views.Build(narrowed));
    CameraRenderData expected;
    views.Fov().Clear();
    (void)views.FovAt(At(110));
    views.Entities.TryGet<CameraComponent>(views.Camera)->FovYRadians = ToRadians(54.0f);
    ASSERT_TRUE(views.Build(expected));
    EXPECT_NEAR(narrowed.Projection[1][1], expected.Projection[1][1], 1e-5f)
        << "a custom 90 degrees, scaled, not overwritten";
    EXPECT_NEAR(narrowed.Projection[0][0], expected.Projection[0][0], 1e-5f);
}

TEST(CameraFovScale, EasesInAndOutOnItsEnvelope)
{
    Views views;
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Attack = 0.1f, .Hold = 0.0f, .Release = 0.1f }, 100, {}),
                       .Scale = 1.5f },
                      At(101));
    EXPECT_FLOAT_EQ(views.FovAt(At(101)), 1.0f);
    EXPECT_FLOAT_EQ(views.FovAt(At(101, 0.0) /* 0 s */), 1.0f);
    EXPECT_NEAR(views.FovAt(At(104)), 1.25f, 1e-4f) << "halfway up the attack";
    EXPECT_NEAR(views.FovAt(At(107)), 1.5f, 1e-4f);
    EXPECT_FLOAT_EQ(views.FovAt(At(120)), 1.0f);
    EXPECT_EQ(views.Fov().Count(), 0u);
}

TEST(CameraFovScale, ZeroIntensityAndOtherViewsKeepTheCamerasOwn)
{
    Views views;
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Hold = 1.0f }, 100, views.Remote), .Scale = 1.3f },
                      At(101));
    EXPECT_FLOAT_EQ(views.FovAt(At(110)), 1.0f) << "not this view's";
    views.Fov().Start({ .Timing = CameraEffectTiming::Once({ .Hold = 1.0f }, 100, views.Pawn), .Scale = 1.3f },
                      At(101));
    EXPECT_FLOAT_EQ(views.FovAt(At(110)), 1.3f);
    views.Fov().Settings.Intensity = 0.0f;
    EXPECT_FLOAT_EQ(views.FovAt(At(110)), 1.0f);
}

TEST(CameraEffectVerbs, ShakeAndFieldOfViewStartFromTheirArgumentsAndHoldsLetGo)
{
    Views views;
    FixedSimulationLoop clock(kTick);
    DataAssetCache profiles;
    CameraShakeProfile profile;
    profile.Kick.Translation = Vec3d(0.0f, 0.1f, 0.0f);
    profile.Envelope = { .Attack = 0.0f, .Hold = 0.0f, .Release = 0.05f };
    (void)profiles.Register(kProfilePath, std::string(kCameraShakeType),
                            std::make_shared<CameraShakeProfile>(profile));
    VerbRegistry catalog;
    VerbDispatcher verbs(catalog);
    verbs.SetDataAssets(&profiles);
    CameraShakeOperations shake(views.Entities, clock, verbs);
    CameraFovScaleOperations fov(views.Entities, clock);

    AuthoredArguments shakeArguments(5);
    shakeArguments.Set(0, AuthoredValue::DataAsset(AssetRef{ AssetType::Data, kProfilePath }));
    shakeArguments.Set(1, AuthoredValue::Float(2.0));
    shakeArguments.Set(2, AuthoredValue::Entity(views.Pawn));
    VerbInvocation call = Call(100, &shakeArguments, { views.Pawn, 1 }, LeaseStep::Acquire);
    EXPECT_EQ(InvokeCameraShakeHold(shake, call), VerbAdmission::Accepted);
    EXPECT_FLOAT_EQ(views.ShakeAt(At(200)).Translation.Y, 0.2f);
    call = Call(200, nullptr, { views.Pawn, 1 }, LeaseStep::Release);
    EXPECT_EQ(InvokeCameraShakeHold(shake, call), VerbAdmission::Accepted);
    (void)views.ShakeAt(At(210));
    EXPECT_EQ(views.Shake().Count(), 0u);

    shakeArguments.Set(0, AuthoredValue::DataAsset(AssetRef{ AssetType::Data, "asset://shakes/missing.sdata" }));
    call = Call(300, &shakeArguments);
    EXPECT_EQ(InvokeCameraShake(shake, call), VerbAdmission::Refused);

    AuthoredArguments fovArguments(5);
    fovArguments.Set(0, AuthoredValue::Float(1.25));
    fovArguments.Set(1, AuthoredValue::Float(0.0));
    fovArguments.Set(2, AuthoredValue::Float(0.1));
    fovArguments.Set(3, AuthoredValue::Float(0.1));
    call = Call(300, &fovArguments);
    EXPECT_EQ(InvokeCameraFovScale(fov, call), VerbAdmission::Accepted);
    EXPECT_FLOAT_EQ(views.FovAt(At(302)), 1.25f);
    fovArguments.Set(2, AuthoredValue::Float(-1.0));
    EXPECT_EQ(InvokeCameraFovScale(fov, call), VerbAdmission::InvalidArguments);
    fovArguments.Set(2, AuthoredValue::Float(0.1));
    fovArguments.Set(0, AuthoredValue::Float(0.0));
    EXPECT_EQ(InvokeCameraFovScale(fov, call), VerbAdmission::InvalidArguments);
}
