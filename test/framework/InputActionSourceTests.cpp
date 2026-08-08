#include <gtest/gtest.h>

#include <ecs/World.h>
#include <input/InputActionSource.h>

//=============================================================================
// Which input source steers an entity.
//
// The rule is one line at every call site -- resolve through the entity rather
// than through the world resource -- and the whole of multiplayer possession
// rests on it. These pin the rule down on its own, away from any session.
//=============================================================================

namespace
{
    constexpr InputActionId kMove{ 1 };
    constexpr InputActionId kJump{ 2 };
    constexpr std::size_t kActionCount = 2;

    World MakeWorld()
    {
        World world;
        world.RegisterComponent<InputActionSourceRef>();
        return world;
    }

    // Writes a distinguishable value into a state's newest tick record.
    void Steer(InputActionState& state, float forward, bool jump)
    {
        const std::span<InputActionValue> values = state.BeginTick(1);
        for (InputActionValue& value : values)
            value = InputActionValue{};
        values[InputActionRegistry::IndexOf(kMove)].Y = forward;
        if (jump)
        {
            values[InputActionRegistry::IndexOf(kJump)].Flags =
                InputActionFlags::Held | InputActionFlags::Fired;
        }
    }
}

TEST(InputActionSources, AnEntityWithNoReferenceReadsTheLocalSource)
{
    World world = MakeWorld();
    InputActionState& local = world.AddResource<InputActionState>();
    local.Configure(kActionCount);
    Steer(local, 1.0f, true);

    const EntityId pawn = world.CreateEntity();

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(pawn).Axis2(kMove).Y, 1.0f);
    EXPECT_TRUE(sources.TickFor(pawn).Fired(kJump));
}

TEST(InputActionSources, AReferencedEntityReadsItsOwnSourceAndNotTheLocalOne)
{
    World world = MakeWorld();
    InputActionState& local = world.AddResource<InputActionState>();
    local.Configure(kActionCount);
    Steer(local, 1.0f, true);

    InputActionSourceTable& table = world.AddResource<InputActionSourceTable>();
    Steer(table.Open(7, kActionCount), -1.0f, false);

    const EntityId mine = world.CreateEntity();
    const EntityId theirs = world.CreateEntity();
    world.AddComponent<InputActionSourceRef>(
        theirs, InputActionSourceRef{ .Source = 7 });

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(mine).Axis2(kMove).Y, 1.0f);
    EXPECT_FLOAT_EQ(sources.TickFor(theirs).Axis2(kMove).Y, -1.0f)
        << "a referenced entity read whoever is at this keyboard";
    EXPECT_TRUE(sources.TickFor(mine).Fired(kJump));
    EXPECT_FALSE(sources.TickFor(theirs).Fired(kJump))
        << "one player's jump reached another player's pawn";
}

// The failure this replaces was silent: a pawn whose source has gone quiet
// would otherwise mirror the local player exactly, which reads as the remote
// player being possessed rather than as a dropped connection.
TEST(InputActionSources, AnUnopenedSourceReadsNothingRatherThanTheLocalOne)
{
    World world = MakeWorld();
    InputActionState& local = world.AddResource<InputActionState>();
    local.Configure(kActionCount);
    Steer(local, 1.0f, true);

    const EntityId orphan = world.CreateEntity();
    world.AddComponent<InputActionSourceRef>(
        orphan, InputActionSourceRef{ .Source = 99 });

    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(orphan).Axis2(kMove).Y, 0.0f);
    EXPECT_FALSE(sources.TickFor(orphan).Fired(kJump));
}

TEST(InputActionSources, ResolveWorksBeforeAnythingHasOpenedATable)
{
    World world = MakeWorld();
    InputActionState& local = world.AddResource<InputActionState>();
    local.Configure(kActionCount);
    Steer(local, 0.5f, false);

    const EntityId pawn = world.CreateEntity();
    const InputActionSources sources(world);
    EXPECT_FLOAT_EQ(sources.TickFor(pawn).Axis2(kMove).Y, 0.5f);
}

TEST(InputActionSourceTable, ClosingASourceRetiresIt)
{
    InputActionSourceTable table;
    table.Open(3, kActionCount);
    ASSERT_NE(table.Find(3), nullptr);
    EXPECT_EQ(table.Size(), 1u);

    table.Close(3);
    EXPECT_EQ(table.Find(3), nullptr);
    EXPECT_EQ(table.Size(), 0u);
}

TEST(InputActionSourceTable, ReopeningKeepsTheSameStorage)
{
    InputActionSourceTable table;
    InputActionState& first = table.Open(3, kActionCount);
    Steer(first, 1.0f, false);

    InputActionState& again = table.Open(3, kActionCount);
    EXPECT_EQ(&first, &again);
    EXPECT_FLOAT_EQ(again.Tick().Axis2(kMove).Y, 1.0f)
        << "resizing to the count it already has cleared the history";
}

// A vocabulary change clears the history rather than reinterpreting it, because
// records indexed by one action set mean nothing under another.
TEST(InputActionSourceTable, ChangingTheVocabularyDropsStaleRecords)
{
    InputActionSourceTable table;
    Steer(table.Open(3, kActionCount), 1.0f, false);

    InputActionState& resized = table.Open(3, kActionCount + 1);
    EXPECT_EQ(resized.HistoryCount(), 0u);
}
