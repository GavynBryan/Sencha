// ParticipantBodySpawns: a participant's prefab body request from the ask,
// through the scene service's asynchronous publication, to its assignment or
// its withdrawal. Exercised against the real lifecycle and spawn service with
// a zero-thread task queue, so every stage is deterministic and no engine boots.

#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <participant/ParticipantLifecycle.h>
#include <participant_spawn/ParticipantBodySpawns.h>
#include <runtime/spawn/SceneSpawnService.h>
#include <world/ComponentRegistrar.h>
#include <world/RuntimeComponentSchema.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SmapFormat.h>
#include <world/serialization/ComponentSerializerRegistry.h>
#include <world/serialization/SceneSerializer.h>
#include <world/transform/TransformComponents.h>

#include "SmapSceneFixture.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    JsonValue TransformAt(double x)
    {
        return JsonValue(JsonValue::Object{
            { "local", JsonValue(JsonValue::Object{
                { "position", JsonValue(JsonValue::Array{
                    JsonValue(x), JsonValue(0.0), JsonValue(0.0) }) },
                { "rotation", JsonValue(JsonValue::Array{
                    JsonValue(0.0), JsonValue(0.0), JsonValue(0.0),
                    JsonValue(1.0) }) },
                { "scale", JsonValue(JsonValue::Array{
                    JsonValue(1.0), JsonValue(1.0), JsonValue(1.0) }) },
            }) } });
    }

    // A pawn-shaped scene: one root and one child parented to it.
    SmapContents PawnContents()
    {
        SmapContents contents;
        SmapEntityRecord root;
        root.Components.emplace_back(MakeComponentTypeId("Transform"), TransformAt(1.0));
        contents.Entities.push_back(std::move(root));
        SmapEntityRecord child;
        child.Parent = 0;
        child.Components.emplace_back(MakeComponentTypeId("Transform"), TransformAt(0.0));
        contents.Entities.push_back(std::move(child));
        return contents;
    }

    // Two parentless entities: not a body, whatever else it is.
    SmapContents TwoRootContents()
    {
        SmapContents contents;
        for (int i = 0; i < 2; ++i)
        {
            SmapEntityRecord root;
            root.Components.emplace_back(MakeComponentTypeId("Transform"),
                                         TransformAt(static_cast<double>(i)));
            contents.Entities.push_back(std::move(root));
        }
        return contents;
    }

    struct Harness
    {
        Harness()
            : Tasks(0)
            , Schema([] {
                WorldComponentSchema schema;
                ComponentRegistrar components(&schema, nullptr, nullptr);
                RegisterEngineComponents(components);
                schema.Seal();
                return schema;
            }())
            , World(Schema)
            , Registry(Logging)
            , Scenes(Logging)
            , SceneLoader(Logging, &Scenes, &Serializers)
            , Assets(Logging, Registry)
            , Service(World, Schema, Serializers, Tasks, Logging)
        {
            RegisterEngineSceneSerializers(Serializers);
            RegisterAssetKind(Assets, AssetType::Scene, SceneLoader, &Scenes);
            Service.ConnectAssets(&Assets, &Scenes);

            Bodies.emplace(
                World.Entities(), Service, Logging.GetLogger<Harness>(),
                [this](const ::World&, EntityId) {
                    ++Selected;
                    return Answer;
                },
                [this](EntityId participant, EntityId body) {
                    if (RefuseAssignment)
                    {
                        ParticipantBodyChange refused;
                        refused.Status = ParticipantBodyStatus::Unavailable;
                        return refused;
                    }
                    return Lifecycle.AssignBody(World.Entities(), participant, body);
                },
                [this](::World& world, EntityId participant, EntityId root) {
                    ++Prepared;
                    PreparedRoot = root;
                    PreparedFor.push_back(participant);
                    // Before assignment, by contract: the participant does not
                    // hold the body yet.
                    const ParticipantControl* control =
                        std::as_const(world).TryGet<ParticipantControl>(participant);
                    EXPECT_TRUE(control != nullptr && !control->Body.IsValid());
                    if (OnPrepare)
                        OnPrepare(world, participant, root);
                });
            Lifecycle.Policies().ProvideBody =
                [this](::World&, EntityId participant) {
                    Bodies->StartSpawn(participant);
                    return EntityId{};
                };
        }

        ~Harness()
        {
            Bodies->Close();
        }

        EntityId Admit()
        {
            return Lifecycle.Admit(World.Entities(), kLocalInputActionSource,
                                   ParticipantPresence::Simulated).Participant;
        }

        void Prefab(std::string path)
        {
            ParticipantBodySpawnRequest spawn;
            spawn.ScenePath = std::move(path);
            Answer = spawn;
        }

        ParticipantBodyChange Request(EntityId participant)
        {
            return Lifecycle.RequestBody(World.Entities(), participant);
        }

        EntityId BodyOf(EntityId participant) const
        {
            const ParticipantControl* control =
                std::as_const(World.Entities()).TryGet<ParticipantControl>(participant);
            return control == nullptr ? EntityId{} : control->Body;
        }

        // One full turn of the async drain: worker runs, completion commits,
        // pump publishes and executes queued despawns.
        void Turn()
        {
            (void)Tasks.PumpWork();
            (void)Tasks.DrainCompletions();
            Service.Pump();
        }

        // A frame as the engine orders it: the drain, then the game's update.
        void Frame()
        {
            Turn();
            Bodies->Update();
        }

        std::size_t Alive() const
        {
            return World.Entities().GetAliveEntities().size();
        }

        LoggingProvider Logging;
        AsyncTaskQueue Tasks;
        ComponentSerializerRegistry Serializers;
        WorldComponentSchema Schema;
        RuntimeWorld World;
        AssetRegistry Registry;
        SceneCache Scenes;
        SceneAssetLoader SceneLoader;
        AssetSystem Assets;
        SceneSpawnService Service;
        ParticipantLifecycle Lifecycle;
        std::optional<ParticipantBodySpawns> Bodies;

        std::optional<ParticipantBodySpawnRequest> Answer;
        std::function<void(::World&, EntityId, EntityId)> OnPrepare;
        bool RefuseAssignment = false;
        int Selected = 0;
        int Prepared = 0;
        EntityId PreparedRoot;
        std::vector<EntityId> PreparedFor;
    };
} // namespace

TEST(ParticipantBodySpawns, SelectingNothingStoresNothingAndRetriesNothing)
{
    Harness h;
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 1);
    for (int i = 0; i < 3; ++i)
        h.Frame();
    EXPECT_EQ(h.Selected, 1) << "an update never re-runs selection on its own";
    EXPECT_EQ(h.Alive(), before);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());

    // A later explicit ask starts work.
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "later");
    h.Prefab(scene.Path);
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 2);
    h.Frame();
    EXPECT_TRUE(h.BodyOf(participant).IsValid());
}

TEST(ParticipantBodySpawns, ALandedPrefabIsPreparedOnceAndAssignedByItsRoot)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "pawn");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();

    // The first ask submits; asking again while it is in flight submits
    // nothing more and runs no second selection.
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 1);
    h.Bodies->Update();
    EXPECT_EQ(h.Prepared, 0);

    h.Frame();

    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(h.Prepared, 1);
    EXPECT_EQ(h.PreparedRoot, body);
    EXPECT_EQ(std::as_const(h.World.Entities()).TryGet<Parent>(body), nullptr)
        << "the body is the group's parentless member";
    EXPECT_FALSE(h.Bodies->CancelPending(participant)) << "nothing is kept once assigned";

    // Settled: further updates and asks do nothing.
    h.Frame();
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::AlreadyAssigned);
    EXPECT_EQ(h.Selected, 1);
    EXPECT_EQ(h.Prepared, 1);
}

TEST(ParticipantBodySpawns, AFailedSpawnIsConsumedWithoutARetryStorm)
{
    Harness h;
    EXPECT_TRUE(h.Registry.Register(AssetRecord{
        .Type = AssetType::Scene,
        .SourceKind = AssetSourceKind::File,
        .Path = "asset://scenes/gone.smap",
        .FilePath = "does/not/exist.smap",
    }));
    h.Prefab("asset://scenes/gone.smap");
    const EntityId participant = h.Admit();

    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    for (int i = 0; i < 3; ++i)
        h.Frame();

    EXPECT_EQ(h.Selected, 1) << "failure does not restart the attempt";
    EXPECT_EQ(h.Prepared, 0);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());

    // The game may ask again; that is a new attempt.
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 2);
}

TEST(ParticipantBodySpawns, AGroupWithTwoRootsIsRefusedBeforeItIsPublished)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, TwoRootContents(), "pair");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "a two-root body was published";
    h.Bodies->Update();

    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    EXPECT_EQ(h.Prepared, 0);
    EXPECT_FALSE(h.Bodies->CancelPending(participant)) << "the failure was consumed";
}

TEST(ParticipantBodySpawns, AParticipantGoneBeforeLandingWithdrawsTheSpawn)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "orphan");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u; // minus the participant

    (void)h.Request(participant);
    h.World.Entities().DestroyEntity(participant);
    h.Bodies->Update();
    h.Turn();

    EXPECT_EQ(h.Alive(), before) << "a withdrawn request publishes nothing";
    EXPECT_EQ(h.Prepared, 0);
}

TEST(ParticipantBodySpawns, CancellingAfterPublicationDestroysTheGroupUnassigned)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "cancel");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Turn();
    EXPECT_EQ(h.Alive(), before + 2u);

    EXPECT_TRUE(h.Bodies->CancelPending(participant));
    EXPECT_FALSE(h.Bodies->CancelPending(participant)) << "idempotent";
    h.Bodies->Update();
    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    EXPECT_EQ(h.Prepared, 0);
    h.Turn();
    EXPECT_EQ(h.Alive(), before);
}

TEST(ParticipantBodySpawns, AnOldCompletionNeverBindsToAReplacementAttempt)
{
    Harness h;
    TempSmapScene first(h.Registry, h.Serializers, PawnContents(), "first");
    TempSmapScene second(h.Registry, h.Serializers, PawnContents(), "second");
    const EntityId participant = h.Admit();

    h.Prefab(first.Path);
    (void)h.Request(participant);
    EXPECT_TRUE(h.Bodies->CancelPending(participant));
    h.Prefab(second.Path);
    (void)h.Request(participant);
    EXPECT_EQ(h.Selected, 2);

    const std::size_t before = h.Alive();
    h.Turn();
    // Only the second request published; the first was withdrawn before
    // the pump reached it.
    EXPECT_EQ(h.Alive(), before + 2u);
    h.Bodies->Update();

    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());
    const LocalTransform* placed =
        std::as_const(h.World.Entities()).TryGet<LocalTransform>(body);
    ASSERT_NE(placed, nullptr);
    EXPECT_FLOAT_EQ(placed->Value.Position.X, 1.0f);
    EXPECT_EQ(h.Prepared, 1);
}

TEST(ParticipantBodySpawns, RetirementReapsTheWholeGroup)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "reap");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Frame();
    ASSERT_TRUE(h.BodyOf(participant).IsValid());

    const ParticipantRetirement retired =
        h.Lifecycle.Retire(h.World.Entities(), participant);
    EXPECT_TRUE(retired.BodyReaped);
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "the root went with the lifecycle, the child with its root";
}

// The lifecycle skips ReapBody for a body that is already dead, so a root
// destroyed by gameplay before its participant retires has to take the
// prefab's children with it through the group's own lifetime.
TEST(ParticipantBodySpawns, ARootDestroyedBeforeRetirementStillCleansItsChildren)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "headless");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Frame();
    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());

    h.World.Entities().DestroyEntity(body);
    const ParticipantRetirement retired =
        h.Lifecycle.Retire(h.World.Entities(), participant);
    EXPECT_EQ(retired.Status, ParticipantRetirementStatus::Retired);
    EXPECT_FALSE(retired.BodyReaped);
    EXPECT_EQ(h.Alive(), before + 1u) << "the child is the leftover";

    h.Turn();
    EXPECT_EQ(h.Alive(), before);
}

TEST(ParticipantBodySpawns, AReapVetoKeepsTheGroupUntilItsRootDies)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "veto");
    h.Prefab(scene.Path);
    h.Lifecycle.Policies().ReapBody =
        [](World&, EntityId, EntityId) { return false; };
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Frame();
    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());

    (void)h.Lifecycle.Retire(h.World.Entities(), participant);
    h.Frame();
    EXPECT_EQ(h.Alive(), before + 2u) << "the vetoed body and its child survive";
    EXPECT_TRUE(h.World.Entities().IsAlive(body));

    h.World.Entities().DestroyEntity(body);
    h.Frame();
    EXPECT_EQ(h.Alive(), before) << "the group follows its root";
}

// Preparation may give the participant a body some other way; the landed
// group is then nobody's and goes, and nothing about it is kept.
TEST(ParticipantBodySpawns, ABodyFoundDuringPreparationEndsTheAttempt)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "beaten");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    EntityId other;
    h.OnPrepare = [&](::World& world, EntityId who, EntityId) {
        other = world.CreateEntity();
        EXPECT_EQ(h.Lifecycle.AssignBody(world, who, other).Status,
                  ParticipantBodyStatus::Assigned);
    };
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Frame();
    EXPECT_EQ(h.BodyOf(participant), other);
    EXPECT_FALSE(h.Bodies->CancelPending(participant));
    h.Turn();
    EXPECT_EQ(h.Alive(), before + 1u) << "only the body found in preparation remains";
}

// Preparation may hand the landed root over itself. Whoever then holds it as a
// body holds its group, so the attempt ends without touching either.
TEST(ParticipantBodySpawns, ARootAssignedDuringPreparationIsKept)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "self");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    h.OnPrepare = [&](::World& world, EntityId who, EntityId root) {
        EXPECT_EQ(h.Lifecycle.AssignBody(world, who, root).Status,
                  ParticipantBodyStatus::Assigned);
    };
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Frame();
    h.Turn();

    EXPECT_EQ(h.BodyOf(participant), h.PreparedRoot);
    EXPECT_TRUE(h.World.Entities().IsAlive(h.PreparedRoot));
    EXPECT_EQ(h.Alive(), before + 2u) << "the assigned body's group was despawned";
    EXPECT_FALSE(h.Bodies->CancelPending(participant));
}

TEST(ParticipantBodySpawns, ARootGivenToAnotherParticipantDuringPreparationIsKept)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "other");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const EntityId other = h.Admit();
    h.OnPrepare = [&](::World& world, EntityId, EntityId root) {
        EXPECT_EQ(h.Lifecycle.AssignBody(world, other, root).Status,
                  ParticipantBodyStatus::Assigned);
    };
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Frame();
    h.Turn();

    EXPECT_EQ(h.BodyOf(other), h.PreparedRoot);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    EXPECT_EQ(h.Alive(), before + 2u) << "another participant's body was despawned";
    EXPECT_FALSE(h.Bodies->CancelPending(participant));
}

TEST(ParticipantBodySpawns, CancellingDuringPreparationAssignsNothing)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "recalled");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    h.OnPrepare = [&](::World&, EntityId who, EntityId) {
        EXPECT_TRUE(h.Bodies->CancelPending(who));
    };
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Frame();
    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    h.Turn();
    EXPECT_EQ(h.Alive(), before);
}

// A refused assignment leaves the group with its caller, which is this adapter.
TEST(ParticipantBodySpawns, ARefusedAssignmentDespawnsTheGroup)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "refused");
    h.Prefab(scene.Path);
    h.RefuseAssignment = true;
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Frame();
    EXPECT_EQ(h.Prepared, 1);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    EXPECT_FALSE(h.Bodies->CancelPending(participant));
    h.Turn();
    EXPECT_EQ(h.Alive(), before);
}

TEST(ParticipantBodySpawns, LandingsInOneFrameAreAssignedInRequestOrder)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "crowd");
    h.Prefab(scene.Path);
    std::vector<EntityId> participants;
    for (int i = 0; i < 6; ++i)
        participants.push_back(h.Admit());
    // Asked in reverse, so request order is not admission order.
    for (auto it = participants.rbegin(); it != participants.rend(); ++it)
        (void)h.Request(*it);

    h.Frame();

    ASSERT_EQ(h.PreparedFor.size(), participants.size());
    EXPECT_TRUE(std::equal(h.PreparedFor.begin(), h.PreparedFor.end(),
                           participants.rbegin()));
}

TEST(ParticipantBodySpawns, CloseWithdrawsPendingAttemptsAndLeavesAssignedBodies)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "close");
    h.Prefab(scene.Path);
    const EntityId landed = h.Admit();
    const EntityId waiting = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(landed);
    h.Frame();
    const EntityId body = h.BodyOf(landed);
    ASSERT_TRUE(body.IsValid());
    (void)h.Request(waiting);
    EXPECT_EQ(h.Alive(), before + 2u);

    h.Bodies->Close();
    h.Bodies->Close();
    h.Turn();
    EXPECT_EQ(h.Alive(), before + 2u) << "the pending spawn is withdrawn, the assigned body kept";
    EXPECT_TRUE(h.World.Entities().IsAlive(body));

    const int selected = h.Selected;
    EXPECT_EQ(h.Request(waiting).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, selected) << "a closed object runs no game callback";
    h.Frame();
    EXPECT_FALSE(h.BodyOf(waiting).IsValid());
}
