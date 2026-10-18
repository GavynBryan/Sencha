#include <app/EngineVocabulary.h>
#include <app/EngineVocabularyBindings.h>
#include <authored/VerbBindingCompiler.h>
#include <authored/VerbDispatcher.h>
#include <authored/WorldVocabulary.h>
#include <movement/CharacterTickStep.h>
#include <movement/DilatedCharacterStepSystem.h>
#include <movement/FreeLocomotionSystem.h>
#include <movement/LocomotionMode.h>
#include <movement/MotionComposition.h>
#include <movement/MovementRegistration.h>
#include <movement/MovementVerbs.h>
#include <movement/components/CharacterMovement.h>
#include <movement/components/MovementTuning.h>
#include <physics/CharacterMoverPool.h>
#include <physics/PhysicsWorld.h>
#include <physics/components/CharacterController.h>
#include <time/ActorTime.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/SimulationAuthority.h>
#include <world/transform/TransformComponents.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>

namespace
{
constexpr float kDt = 1.0f / 60.0f;

class MovementVelocity : public testing::Test
{
protected:
    MovementVelocity()
    {
        ComponentRegistrar registrar(Entities);
        RegisterEngineComponents(registrar);
        RegisterMovement(Entities);
        InstallAuthoredVocabulary(Entities);
        EXPECT_TRUE(InstallEngineVocabulary(Entities));
        Verbs = std::make_unique<VerbDispatcher>(*FindVerbRegistry(Entities));
        Bindings = std::make_unique<EngineVocabularyBindings>(Entities, Clock, *Verbs);
        Character = Entities.CreateEntity();
        Entities.AddComponent<LocalTransform>(Character, LocalTransform{});
        Entities.AddComponent<CharacterController>(Character, CharacterController{});
        Entities.AddComponent<CharacterMovement>(Character,
            CharacterMovement{ .Mode = Entities.GetResource<LocomotionModeRegistry>().FreeMode() });
        auto* tuning = Entities.TryGet<ResolvedMovementTuning>(Character);
        tuning->Friction = 0;
        tuning->Drag = 0;
        tuning->Acceleration = 0;
        tuning->GravityScale = 1;
        Partitions.Add(StoragePartitionId::Default());
        Movers.Reconcile(Entities, Partitions);
    }

    VerbAdmission Apply(Vec3d direction, double speed, std::string operation = "replace")
    {
        VerbBindingDesc desc;
        desc.Key = "velocity";
        desc.VerbName = std::string(kMovementApplyVelocityVerb);
        desc.Inputs = { "activator" };
        desc.Arguments = {
            { .Key = "target", .Source = VerbArgumentSource::Input, .Literal = {}, .Text = "activator" },
            { .Key = "direction", .Literal = JsonValue(JsonValue::Array{
                JsonValue(double(direction.X)), JsonValue(double(direction.Y)), JsonValue(double(direction.Z)) }), .Text = {} },
            { .Key = "speed", .Literal = JsonValue(speed), .Text = {} },
            { .Key = "operation", .Literal = JsonValue(std::move(operation)), .Text = {} },
        };
        CompiledVerbBinding binding;
        std::vector<std::string> errors;
        if (!CompileVerbBinding(desc, MakeVerbBindingEnvironment(Entities), binding, errors))
            return VerbAdmission::InvalidArguments;
        const std::array inputs{ AuthoredValue::Entity(Character) };
        return Verbs->Invoke(binding, inputs).Status;
    }

    void Step(float dt = kDt)
    {
        Locomotion.Step(Entities, dt);
        Composition.Step(Entities);
        Movers.Drive(Entities, Partitions, dt, Vec3d(0, -9.81f, 0));
    }

    Vec3d Velocity() const { return Entities.TryGet<KinematicState>(Character)->Velocity; }
    Vec3d Position() const { return Entities.TryGet<LocalTransform>(Character)->Value.Position; }

    World Entities;
    FixedSimulationLoop Clock{ kDt };
    std::unique_ptr<VerbDispatcher> Verbs;
    std::unique_ptr<EngineVocabularyBindings> Bindings;
    PhysicsWorld Physics;
    CharacterMoverPool Movers{ Physics };
    FreeLocomotionSystem Locomotion{ Vec3d(0, -9.81f, 0), Vec3d(0, 1, 0) };
    MotionCompositionSystem Composition;
    StoragePartitionSet Partitions;
    EntityId Character;
};
}

TEST_F(MovementVelocity, ReplacePersistsThroughMotorAndFollowingTicks)
{
    Entities.TryGet<KinematicState>(Character)->Velocity = Vec3d(9, -3, 7);
    auto* support = Entities.TryGet<SupportState>(Character);
    support->Kind = SupportKind::Stable;
    support->SurfaceVelocity = Vec3d(1, 4, 2);
    ASSERT_EQ(Apply(Vec3d(0, 2, 0), 17), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Velocity(), Vec3d(0, 17, 0));
    EXPECT_GT(Position().Y, 0);
    const float firstY = Position().Y;
    Step();
    EXPECT_NEAR(Velocity().Y, 17 - 9.81f * kDt, 0.001f);
    EXPECT_GT(Position().Y, firstY);
    for (int i = 0; i < 10; ++i)
        Step();
    EXPECT_GT(Velocity().Y, 14);
}

TEST_F(MovementVelocity, AddPreservesExistingVelocity)
{
    Entities.TryGet<KinematicState>(Character)->Velocity = Vec3d(9, 3, 7);
    ASSERT_EQ(Apply(Vec3d(0, 1, 0), 4, "add"), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Velocity().X, 9);
    EXPECT_EQ(Velocity().Z, 7);
    EXPECT_NEAR(Velocity().Y, 7 - 9.81f * kDt, 0.001f);
    Step();
    EXPECT_NEAR(Velocity().Y, 7 - 2 * 9.81f * kDt, 0.001f);
}

TEST_F(MovementVelocity, OrderedChangesReplaceEarlierImpulsesAndRetainLaterOnes)
{
    ASSERT_EQ(Apply(Vec3d(1, 0, 0), 20, "add"), VerbAdmission::Accepted);
    ASSERT_EQ(Apply(Vec3d(0, 1, 0), 17), VerbAdmission::Accepted);
    ASSERT_EQ(Apply(Vec3d(0, 0, 1), 3, "add"), VerbAdmission::Accepted);
    Step();
    EXPECT_EQ(Velocity(), Vec3d(0, 17, 3));
}

TEST_F(MovementVelocity, InvalidDirectionSpeedAndOperationDoNotMutateMotion)
{
    EXPECT_EQ(Apply(Vec3d::Zero(), 17), VerbAdmission::InvalidArguments);
    EXPECT_EQ(Apply(Vec3d(0, 1, 0), -1), VerbAdmission::InvalidArguments);
    EXPECT_EQ(Apply(Vec3d(0, 1, 0), std::numeric_limits<double>::infinity()), VerbAdmission::InvalidArguments);
    EXPECT_EQ(Apply(Vec3d(0, 1, 0), 17, "invalid"), VerbAdmission::InvalidArguments);
    EXPECT_FALSE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
    EXPECT_EQ(Entities.TryGet<MotionImpulse>(Character)->DeltaVelocity, Vec3d::Zero());
}

TEST_F(MovementVelocity, ClientsCannotAdmitAuthoritativeVelocityChanges)
{
    Entities.AddResource<SimulationAuthority>(SimulationAuthority{ .Authoritative = false });
    EXPECT_EQ(Apply(Vec3d(0, 1, 0), 17), VerbAdmission::Refused);
    EXPECT_FALSE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
}

TEST_F(MovementVelocity, DilatedLiveTickConsumesVelocityChangeOnceAcrossSubsteps)
{
    Entities.AddComponent<ActorTimeDilated>(Character, ActorTimeDilated{});
    auto* time = Entities.TryGet<ActorTime>(Character);
    ASSERT_NE(time, nullptr);
    time->ScaleQ16 = kActorTimeOne * 3;
    ASSERT_EQ(Apply(Vec3d(0, 1, 0), 17), VerbAdmission::Accepted);
    Composition.Step(Entities);
    EXPECT_TRUE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
    EngineConfig config;
    RuntimeFrameLoop runtime;
    FixedLogicContext context{ config, runtime, { kDt, 1 }, Entities, Partitions };
    DilatedCharacterStepSystem dilated(Locomotion, nullptr);
    dilated.FixedLogic(context);
    EXPECT_NEAR(Velocity().Y, 17 - 2 * 9.81f * kDt, 0.001f);
    EXPECT_FALSE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
    dilated.FixedLogic(context);
    EXPECT_NEAR(Velocity().Y, 17 - 5 * 9.81f * kDt, 0.001f);
}

TEST_F(MovementVelocity, LaunchPersistsAtSupportedFixedSteps)
{
    for (const float dt : { 1.0f / 30, 1.0f / 60, 1.0f / 120 })
    {
        ASSERT_TRUE(Movers.SetPosition(Entities, Character, Vec3d::Zero()));
        Entities.TryGet<KinematicState>(Character)->Velocity = Vec3d::Zero();
        ASSERT_EQ(Apply(Vec3d(0, 1, 0), 17), VerbAdmission::Accepted);
        Step(dt);
        EXPECT_EQ(Velocity(), Vec3d(0, 17, 0));
        for (int i = 0; i < 4; ++i)
            Step(dt);
        EXPECT_NEAR(Velocity().Y, 17 - 4 * 9.81f * dt, 0.001f);
        EXPECT_GT(Position().Y, 0);
    }
}

TEST_F(MovementVelocity, PausedActorRetainsChangeUntilItsClockAdvances)
{
    Entities.AddComponent<ActorTimeDilated>(Character, ActorTimeDilated{});
    auto* time = Entities.TryGet<ActorTime>(Character);
    time->ScaleQ16 = 0;
    ASSERT_EQ(Apply(Vec3d(0, 1, 0), 17), VerbAdmission::Accepted);
    EngineConfig config;
    RuntimeFrameLoop runtime;
    FixedLogicContext context{ config, runtime, { kDt, 1 }, Entities, Partitions };
    DilatedCharacterStepSystem dilated(Locomotion, nullptr);
    Composition.Step(Entities);
    dilated.FixedLogic(context);
    EXPECT_TRUE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
    EXPECT_EQ(Position(), Vec3d::Zero());
    time->ScaleQ16 = kActorTimeOne / 2;
    dilated.FixedLogic(context);
    EXPECT_EQ(Velocity(), Vec3d(0, 17, 0));
    EXPECT_GT(Position().Y, 0);
    EXPECT_FALSE(Entities.TryGet<MotionImpulse>(Character)->HasReplacement);
}
