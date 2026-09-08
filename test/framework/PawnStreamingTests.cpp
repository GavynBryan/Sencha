#include <gtest/gtest.h>

#include <ecs/World.h>
#include <participant/LocalControl.h>
#include <world/RuntimeWorld.h>
#include <world/transform/TransformComponents.h>
#include <zone/WorldPartitionRuntime.h>

#include "../runtime/StreamingTraversalFixture.h"
#include "PawnStreaming.h"

//=============================================================================
// The template's streaming policy: the world stays loaded around whoever this
// machine drives. The engine streams around a focus and does not decide whose;
// this is the whole of the deciding.
//=============================================================================
namespace
{
    using StreamingTraversal::Harness;
    using StreamingTraversal::kZoneCount;
    using StreamingTraversal::ZoneAt;

    Vec3d InZone(int index)
    {
        return Vec3d{ static_cast<float>(index * StreamingTraversal::kZoneSpan
                                         + StreamingTraversal::kZoneSpan * 0.5),
                      1.0f, 0.0f };
    }

    struct PawnStreamingTest : ::testing::Test
    {
        void SetUp() override
        {
            ASSERT_EQ(Rig.LoadManifest(), "");
        }

        EntityId PawnIn(int zoneIndex)
        {
            World& world = Rig.World().Entities();
            const EntityId pawn = world.CreateEntity();
            Transform3f placed;
            placed.Position = InZone(zoneIndex);
            world.AddComponent<WorldTransform>(pawn, WorldTransform{ placed });
            return pawn;
        }

        // A frame the way the engine runs it: the game's focus lands in the
        // residency hook, so it feeds the *next* partition update.
        void Step(int frames = 4)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                FocusStreamingOnLocalPawn(Rig.World().Entities(), Rig.Partition());
                Rig.StepFrame();
            }
            Rig.SettleLoads();
        }

        [[nodiscard]] bool Resident(int zoneIndex)
        {
            return Rig.World().FindZone(ZoneAt(zoneIndex)) != nullptr;
        }

        Harness Rig{ 0, kZoneCount };
    };
}

TEST_F(PawnStreamingTest, TheWorldFollowsTheDrivenBody)
{
    const EntityId pawn = PawnIn(3);
    (void)SetLocalControlSubject(Rig.World().Entities(), pawn);

    Step();

    EXPECT_TRUE(Resident(3)) << "the room the player is standing in is not loaded";
    EXPECT_EQ(Rig.Partition().FocusZone(), ZoneAt(3));
}

TEST_F(PawnStreamingTest, MovingTheBodyMovesTheFocus)
{
    const EntityId pawn = PawnIn(2);
    World& world = Rig.World().Entities();
    (void)SetLocalControlSubject(world, pawn);
    Step();
    ASSERT_EQ(Rig.Partition().FocusZone(), ZoneAt(2));

    world.TryGet<WorldTransform>(pawn)->Value.Position = InZone(3);
    Step();

    EXPECT_EQ(Rig.Partition().FocusZone(), ZoneAt(3));
    EXPECT_TRUE(Resident(3));
}

// Nobody is driving anything: a spectator process, or a game before its pawn
// arrived. The focus is left where the load put it rather than yanked to
// the origin.
TEST_F(PawnStreamingTest, NoDrivenBodyLeavesTheFocusAlone)
{
    (void)PawnIn(5);
    Rig.Partition().SetFocus(ZoneAt(1));

    Step();

    EXPECT_EQ(Rig.Partition().FocusZone(), ZoneAt(1));
}
