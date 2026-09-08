// BodySpawns: one owner for a prefab body request from the ask,
// through the scene service's asynchronous publication, to the handoff into
// the participant lifecycle or the cleanup of what was never handed over.
// Exercised against the real lifecycle and spawn service with a zero-thread
// task queue, so every stage is deterministic and no engine boots.

#include <app/BodySpawns.h>
#include <assets/runtime/AssetSystem.h>
#include <assets/runtime/RegisterAssetKind.h>
#include <assets/scene/SceneAssetLoader.h>
#include <assets/scene/SceneCache.h>
#include <core/logging/LoggingProvider.h>
#include <ecs/World.h>
#include <ecs/WorldComponentSchema.h>
#include <jobs/AsyncTaskQueue.h>
#include <participant/ParticipantLifecycle.h>
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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

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
                [this](EntityId participant) {
                    return Lifecycle.RequestBody(World.Entities(), participant);
                },
                [this](::World& world, EntityId participant, EntityId root) {
                    ++Prepared;
                    PreparedRoot = root;
                    // Before assignment, by contract: the participant does not
                    // hold the body yet.
                    const ParticipantControl* control =
                        std::as_const(world).TryGet<ParticipantControl>(participant);
                    EXPECT_TRUE(control != nullptr && !control->Body.IsValid());
                });
            Lifecycle.Policies().ProvideBody =
                [this](::World&, EntityId participant) {
                    return Bodies->ProvideBody(participant);
                };
            Lifecycle.Policies().ReapBody =
                [this](::World&, EntityId, EntityId body) {
                    (void)Bodies->RequestDespawnBody(body);
                    return true;
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
            BodySpawnRequest spawn;
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
        std::optional<BodySpawns> Bodies;

        std::optional<BodySpawnRequest> Answer;
        int Selected = 0;
        int Prepared = 0;
        EntityId PreparedRoot;
    };
} // namespace

TEST(BodySpawns, SelectingNothingStoresNothingAndRetriesNothing)
{
    Harness h;
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 1);
    for (int i = 0; i < 3; ++i)
    {
        h.Bodies->Update();
        h.Turn();
    }
    EXPECT_EQ(h.Selected, 1) << "an update never re-runs selection on its own";
    EXPECT_EQ(h.Alive(), before);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());

    // A later explicit ask starts work.
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "later");
    h.Prefab(scene.Path);
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 2);
    h.Turn();
    h.Bodies->Update();
    EXPECT_TRUE(h.BodyOf(participant).IsValid());
}

TEST(BodySpawns, ALandedPrefabIsPreparedOnceAndHandedOverByItsRoot)
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

    h.Turn();
    h.Bodies->Update();

    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());
    EXPECT_EQ(h.Prepared, 1);
    EXPECT_EQ(h.PreparedRoot, body);
    EXPECT_EQ(std::as_const(h.World.Entities()).TryGet<Parent>(body), nullptr)
        << "the body is the group's parentless member";
    EXPECT_EQ(h.Selected, 1);

    // Settled: further updates and asks do nothing.
    h.Bodies->Update();
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::AlreadyAssigned);
    EXPECT_EQ(h.Selected, 1);
    EXPECT_EQ(h.Prepared, 1);
}

TEST(BodySpawns, AFailedSpawnIsConsumedWithoutARetryStorm)
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
    h.Turn();
    for (int i = 0; i < 3; ++i)
        h.Bodies->Update();

    EXPECT_EQ(h.Selected, 1) << "failure does not restart the attempt";
    EXPECT_EQ(h.Prepared, 0);
    EXPECT_FALSE(h.BodyOf(participant).IsValid());

    // The game may ask again; that is a new attempt.
    EXPECT_EQ(h.Request(participant).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, 2);
}

TEST(BodySpawns, AGroupWithTwoRootsIsRefusedAndRemoved)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, TwoRootContents(), "pair");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(participant);
    h.Turn();
    EXPECT_EQ(h.Alive(), before + 2u);
    h.Bodies->Update();

    EXPECT_FALSE(h.BodyOf(participant).IsValid());
    EXPECT_EQ(h.Prepared, 0);
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "the refused group is destroyed whole";
}

TEST(BodySpawns, AParticipantGoneBeforeLandingWithdrawsTheSpawn)
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

TEST(BodySpawns, CancellingAfterPublicationDestroysTheGroupUnassigned)
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

TEST(BodySpawns, AnOldCompletionNeverBindsToAReplacementAttempt)
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

TEST(BodySpawns, RetirementReapsTheWholeGroup)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "reap");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Turn();
    h.Bodies->Update();
    ASSERT_TRUE(h.BodyOf(participant).IsValid());

    const ParticipantRetirement retired =
        h.Lifecycle.Retire(h.World.Entities(), participant);
    EXPECT_TRUE(retired.BodyReaped);
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "the root went with the lifecycle, the child with the group";
}

// The lifecycle skips ReapBody for a body that is already dead, so a root
// destroyed by gameplay before its participant retires used to leave the
// prefab's children standing, outside any record that knew them.
TEST(BodySpawns, ARootDestroyedBeforeRetirementStillCleansItsChildren)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "headless");
    h.Prefab(scene.Path);
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Turn();
    h.Bodies->Update();
    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());

    h.World.Entities().DestroyEntity(body);
    const ParticipantRetirement retired =
        h.Lifecycle.Retire(h.World.Entities(), participant);
    EXPECT_EQ(retired.Status, ParticipantRetirementStatus::Retired);
    EXPECT_FALSE(retired.BodyReaped);
    EXPECT_EQ(h.Alive(), before + 1u) << "the child is the leftover";

    h.Bodies->Update();
    h.Turn();
    EXPECT_EQ(h.Alive(), before);
}

TEST(BodySpawns, AReapVetoKeepsTheGroupUntilItsRootDies)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "veto");
    h.Prefab(scene.Path);
    h.Lifecycle.Policies().ReapBody =
        [](World&, EntityId, EntityId) { return false; };
    const EntityId participant = h.Admit();
    const std::size_t before = h.Alive() - 1u;

    (void)h.Request(participant);
    h.Turn();
    h.Bodies->Update();
    const EntityId body = h.BodyOf(participant);
    ASSERT_TRUE(body.IsValid());

    (void)h.Lifecycle.Retire(h.World.Entities(), participant);
    h.Bodies->Update();
    h.Turn();
    EXPECT_EQ(h.Alive(), before + 2u) << "the vetoed body and its child survive";
    EXPECT_TRUE(h.World.Entities().IsAlive(body));

    h.World.Entities().DestroyEntity(body);
    h.Bodies->Update();
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "the group follows its root";
}

TEST(BodySpawns, CloseEndsEveryAttemptAndAnswersNothingAfter)
{
    Harness h;
    TempSmapScene scene(h.Registry, h.Serializers, PawnContents(), "close");
    h.Prefab(scene.Path);
    const EntityId landed = h.Admit();
    const EntityId waiting = h.Admit();
    const std::size_t before = h.Alive();

    (void)h.Request(landed);
    h.Turn();
    h.Bodies->Update();
    ASSERT_TRUE(h.BodyOf(landed).IsValid());
    (void)h.Request(waiting);
    EXPECT_EQ(h.Alive(), before + 2u);

    h.Bodies->Close();
    h.Bodies->Close();
    h.Turn();
    EXPECT_EQ(h.Alive(), before) << "the handed-over group and the pending one are both gone";

    const int selected = h.Selected;
    EXPECT_EQ(h.Request(waiting).Status, ParticipantBodyStatus::Unavailable);
    EXPECT_EQ(h.Selected, selected) << "a closed object runs no game callback";
    h.Bodies->Update();
    EXPECT_FALSE(h.Bodies->RequestDespawnBody(h.BodyOf(landed)));
}
