#include "TemplateModuleRun.h"

#include <anim/AnimationClipPlaybackSystem.h>
#include <authored/VerbBindingSet.h>
#include <authored/VerbDispatcher.h>
#include <logic/VerbRelay.h>
#include <core/console/ConsoleService.h>
#include <ecs/World.h>
#include <net/NetParticipantIdentity.h>
#include <net/NetReplicationComponents.h>
#include <net/NetSpawnPrefab.h>
#include <participant/ParticipantControl.h>
#include <world/RuntimeWorld.h>
#include <world/scene/SceneInstance.h>
#include <world/scene/SceneInstanceIndex.h>

#include <algorithm>
#include <vector>

//=============================================================================
// The arena template's composition: a pawn that exists before any session
// carries the prefab identity a later host will need and no replication state,
// and hosting afterwards gives the player who was already here what a
// session-time admission would have.
//=============================================================================
#if !defined(TEST_ARENA_MODULE_PATH)
TEST(ArenaTemplate, RequiresTheArenaModule)
{
    GTEST_SKIP() << "arena is not in SENCHA_BUILD_TEMPLATES";
}
#else
namespace
{
    struct ArenaProbe
    {
        Engine* Host = nullptr;
        // No starter content plays clips; the template must not pay for playback.
        bool AnimationRegistered = true;
        int Frames = 0;
        static constexpr int kHostAtFrame = 150;

        // Observed on the frame before `host` runs.
        bool BodyBeforeHost = false;
        bool PrefabNamedBeforeHost = false;
        bool ReplicatedBeforeHost = true;
        bool ParticipantReplicatedBeforeHost = true;
        bool Hosted = false;
        // Observed on every frame after.
        bool ReplicatedAfterHost = false;
        bool ParticipantReplicatedAfterHost = false;
        bool ParticipantIsAuthoritys = false;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            AnimationRegistered = Host->Schedule().Has<AnimationClipPlaybackSystem>();
            const World& world = ctx.Entities;
            ++Frames;
            EntityId participant;
            EntityId body;
            if (world.IsRegistered<ParticipantControl>())
            {
                world.ForEachComponent<ParticipantControl>(
                    [&](EntityId entity, const ParticipantControl& control) {
                        participant = entity;
                        body = control.Body;
                    });
            }
            const bool bodyAlive = body.IsValid() && world.IsAlive(body);

            if (Frames < kHostAtFrame)
            {
                BodyBeforeHost = bodyAlive;
                if (!bodyAlive)
                    return;
                PrefabNamedBeforeHost = world.HasComponent<NetSpawnPrefab>(body);
                ReplicatedBeforeHost = world.HasComponent<NetReplicated>(body);
                ParticipantReplicatedBeforeHost =
                    world.HasComponent<NetReplicated>(participant);
                return;
            }
            if (Frames == kHostAtFrame && Host != nullptr)
            {
                const ConsoleResult opened = Host->Console().ExecuteTokens(
                    { "host", "0" }, ConsoleValueSource{ .Description = "arena test" });
                Hosted = opened.Status == ConsoleStatus::Ok;
                return;
            }
            if (!bodyAlive)
                return;
            ReplicatedAfterHost = world.HasComponent<NetReplicated>(body);
            ParticipantReplicatedAfterHost = world.HasComponent<NetReplicated>(participant);
            const NetParticipantIdentity* identity =
                world.TryGet<NetParticipantIdentity>(participant);
            ParticipantIsAuthoritys = identity != nullptr && identity->Peer == kNetAuthorityPeer;
        }
    };
}

TEST(ArenaTemplate, APawnBeforeTheSessionIsStampedWhenHostingStarts)
{
    TemplateModuleRun<ArenaProbe> run(
        TEST_ARENA_MODULE_PATH, SENCHA_REPO_ROOT "/templates/arena/assets",
        "levels/arena_room", 260);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);

    const ArenaProbe& seen = run.Seen();
    EXPECT_FALSE(seen.AnimationRegistered) << "no starter content plays clips";
    ASSERT_TRUE(seen.BodyBeforeHost) << "the prefab pawn never landed before hosting";
    EXPECT_TRUE(seen.PrefabNamedBeforeHost)
        << "the body names its prefab so a later host can tell peers how to build it";
    EXPECT_FALSE(seen.ReplicatedBeforeHost) << "nothing is replicated with nobody to replicate to";
    EXPECT_FALSE(seen.ParticipantReplicatedBeforeHost);
    ASSERT_TRUE(seen.Hosted) << "`host 0` was refused";
    EXPECT_TRUE(seen.ReplicatedAfterHost) << "hosting stamped the body that was already here";
    EXPECT_TRUE(seen.ParticipantReplicatedAfterHost);
    EXPECT_TRUE(seen.ParticipantIsAuthoritys);
}

namespace
{
    struct ReapedRootProbe
    {
        Engine* Host = nullptr;
        bool Retired = false;
        int UpdatesAfterRetirement = 0;
        std::vector<EntityId> Children;
        bool ChildrenGone = false;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            World& world = ctx.Entities;
            if (Retired)
            {
                ++UpdatesAfterRetirement;
                ChildrenGone = std::all_of(Children.begin(), Children.end(),
                    [&](EntityId child) { return !world.IsAlive(child); });
                if (UpdatesAfterRetirement == 4)
                    Host->RequestExit();
                return;
            }

            const EntityId participant = LocalParticipantOf(world);
            const ParticipantControl* control =
                std::as_const(world).TryGet<ParticipantControl>(participant);
            if (control == nullptr || !world.IsAlive(control->Body))
                return;
            const EntityId body = control->Body;
            const SceneInstanceId group = world.TryGet<SceneInstance>(body)->Id;
            for (EntityId member : world.GetResource<SceneInstanceIndex>().Entities(group))
                if (member != body)
                    Children.push_back(member);

            world.DestroyEntity(body);
            Retired = Host->RetireParticipant(participant).Status
                == SessionParticipantRetirementStatus::Retired;
        }
    };
}

TEST(ArenaTemplate, RetirementAfterRootDestructionStillCleansPrefabChildren)
{
    TemplateModuleRun<ReapedRootProbe> run(
        TEST_ARENA_MODULE_PATH, SENCHA_REPO_ROOT "/templates/arena/assets",
        "levels/arena_room", 260);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);
    ASSERT_TRUE(run.Seen().Retired);
    ASSERT_FALSE(run.Seen().Children.empty());
    EXPECT_GE(run.Seen().UpdatesAfterRetirement, 3);
    EXPECT_TRUE(run.Seen().ChildrenGone);
}

namespace
{
    // One game verb reached two ways: the level's placed relay, activated by a
    // native console path, and the binding the shell's menu entry addresses,
    // invoked through the same dispatcher a click would reach. Both carry the
    // same schema; only the relay names an entity, and it names itself.
    struct ScoreProbe
    {
        Engine* Host = nullptr;
        int Frames = 0;
        static constexpr int kAwardAtFrame = 60;
        static constexpr int kMenuAtFrame = 90;
        static constexpr int kReadAtFrame = 150;

        bool RelayPlaced = false;
        bool RelayActivated = false;
        bool ShellBindingResolved = false;
        VerbAdmission MenuAdmission = VerbAdmission::Unavailable;
        std::string Scoreboard;

        void FrameUpdate(FrameUpdateContext& ctx)
        {
            World& world = ctx.Entities;
            ++Frames;
            if (Frames == kAwardAtFrame)
            {
                if (world.IsRegistered<VerbRelay>())
                {
                    world.ForEachComponent<VerbRelay>(
                        [this](EntityId, const VerbRelay& relay) {
                            RelayPlaced = relay.Bindings.IsValid() && relay.Binding.IsValid();
                        });
                }
                const ConsoleResult awarded = Host->Console().ExecuteTokens(
                    { "award" }, ConsoleValueSource{ .Description = "arena test" });
                RelayActivated = awarded.Status == ConsoleStatus::Ok;
                return;
            }
            if (Frames == kMenuAtFrame)
            {
                const CompiledVerbBinding* binding =
                    Host->ShellBindings().Find("arena.award_red");
                ShellBindingResolved = binding != nullptr;
                if (binding != nullptr && Host->TryVerbs() != nullptr)
                    MenuAdmission = Host->TryVerbs()->Invoke(*binding, {}).Status;
                return;
            }
            if (Frames == kReadAtFrame)
            {
                // Read at the module boundary, the way a player would: the
                // scoreboard is the game's type, and this binary links only
                // the engine.
                const ConsoleResult score = Host->Console().ExecuteTokens(
                    { "score" }, ConsoleValueSource{ .Description = "arena test" });
                for (const ConsoleOutputEntry& entry : score.Output)
                    Scoreboard += entry.Text;
            }
        }
    };
}

TEST(ArenaTemplate, OneAuthoredVerbIsReachedFromTheRelayAndFromTheShellsBinding)
{
    TemplateModuleRun<ScoreProbe> run(
        TEST_ARENA_MODULE_PATH, SENCHA_REPO_ROOT "/templates/arena/assets",
        "levels/arena_room", 200);
    ASSERT_TRUE(run.Loaded());
    ASSERT_EQ(run.Exit(), 0);

    const ScoreProbe& seen = run.Seen();
    EXPECT_TRUE(seen.RelayPlaced) << "the cooked level carries no verb_relay";
    EXPECT_TRUE(seen.RelayActivated) << "`award` was refused";
    EXPECT_TRUE(seen.ShellBindingResolved)
        << "the game's binding asset did not land in the shell's set";
    EXPECT_EQ(seen.MenuAdmission, VerbAdmission::Accepted);
    // The relay's binding awards blue five, with the relay as its source; the
    // menu's awards red one, with none.
    EXPECT_EQ(seen.Scoreboard, "red 1, blue 5");
}
#endif
